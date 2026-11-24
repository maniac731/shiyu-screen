#include "photo_storage.h"
#include "carousel_settings.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if !CONFIG_FATFS_LFN_HEAP || CONFIG_FATFS_MAX_LFN < 40
#error "Cloud photo UUID filenames require heap-based FAT long filenames (>=40)"
#endif
#define LIMIT 20
#define BUDGET (8U*1024U*1024U)
typedef struct { char id[37]; uint32_t bytes; } entry_t;
typedef struct { uint32_t version,generation,count; entry_t entries[LIMIT]; uint32_t checksum; } catalog_t;
static catalog_t s_cat, s_disk[2];
static SemaphoreHandle_t s_lock;
static unsigned s_index;
static char s_shown[37],s_previous[37];
static wl_handle_t s_wl_handle=WL_INVALID_HANDLE;
static bool validate_cloud_jpeg(const char *path);
static bool valid_id(const char *id) { if(!id || strnlen(id,37)!=36)return false; for(int i=0;i<36;i++)if((i==8||i==13||i==18||i==23)?id[i]!='-':!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false; return true; }
static void path_for(const char *id,char *out,size_t n){snprintf(out,n,"/storage/%s.jpg",id);}
static uint32_t checksum(const catalog_t *c){const unsigned char *p=(const void*)c;uint32_t h=2166136261u;for(size_t i=0;i<offsetof(catalog_t,checksum);i++)h=(h^p[i])*16777619u;return h;}
static bool valid_catalog(catalog_t *c){if(c->version!=2||c->count>LIMIT||c->checksum!=checksum(c))return false;uint32_t total=0;for(unsigned i=0;i<c->count;i++){char path[64];if(!valid_id(c->entries[i].id))return false;path_for(c->entries[i].id,path,sizeof(path));struct stat st;if(stat(path,&st)||st.st_size!=c->entries[i].bytes||!validate_cloud_jpeg(path))return false;total+=st.st_size;}return total<=BUDGET;}
static bool publish(catalog_t *next){next->version=2;next->generation=s_cat.generation+1;next->checksum=checksum(next);unsigned slot=next->generation%2;nvs_handle_t n; if(nvs_open("cloud_photo",NVS_READWRITE,&n)!=ESP_OK)return false;esp_err_t e=nvs_set_blob(n,slot?"catalog1":"catalog0",next,sizeof(*next));if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);if(e!=ESP_OK)return false;s_disk[slot]=*next;xSemaphoreTake(s_lock,portMAX_DELAY);s_cat=*next;if(s_index>=s_cat.count)s_index=0;xSemaphoreGive(s_lock);ESP_LOGI("photo_storage", "Cached photos: %u", (unsigned)next->count);return true;}
/* Keep large migration scratch off the 3584-byte main-task stack. */
static void migrate_single_photo(nvs_handle_t nvs)
{
    struct { uint32_t version, slot; char id[37]; } old = {0};
    size_t size = sizeof(old);
    if (nvs_get_blob(nvs, "active", &old, &size) != ESP_OK || size != sizeof(old) ||
        old.version != 1 || old.slot > 1 || !valid_id(old.id)) return;

    char source[32], destination[64];
    snprintf(source, sizeof(source), "/storage/cloud%u.jpg", (unsigned)old.slot);
    path_for(old.id, destination, sizeof(destination));
    if (!validate_cloud_jpeg(source)) return;

    char *buffer = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    catalog_t *next = heap_caps_calloc(1, sizeof(*next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer || !next) {
        ESP_LOGW("photo_storage", "Migration allocation failed; keeping original cache");
        free(buffer); free(next);
        return;
    }
    FILE *in = fopen(source, "rb");
    FILE *out = in ? fopen(destination, "wb") : NULL;
    bool ok = in && out;
    size_t count;
    while (ok && (count = fread(buffer, 1, 2048, in)) > 0)
        ok = fwrite(buffer, 1, count, out) == count;
    if (in) { if (ferror(in)) ok = false; fclose(in); }
    if (out) {
        if (fflush(out)) ok = false;
        if (fclose(out)) ok = false;
    }
    struct stat st;
    if (ok && validate_cloud_jpeg(destination) && stat(destination, &st) == 0) {
        next->count = 1;
        strlcpy(next->entries[0].id, old.id, sizeof(next->entries[0].id));
        next->entries[0].bytes = st.st_size;
        if (!publish(next)) ESP_LOGW("photo_storage", "Migration catalog save failed; original retained");
    }
    free(buffer); free(next);
}

bool photo_storage_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    const esp_vfs_fat_mount_config_t config = {
        .max_files = 5, .format_if_mount_failed = false,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };
    if (esp_vfs_fat_spiflash_mount_rw_wl(PHOTO_STORAGE_MOUNT_POINT, "storage", &config, &s_wl_handle) != ESP_OK)
        return false;
    nvs_handle_t nvs;
    if (nvs_open("cloud_photo", NVS_READONLY, &nvs) == ESP_OK) {
        for (unsigned i = 0; i < 2; ++i) {
            size_t size = sizeof(catalog_t);
            if (nvs_get_blob(nvs, i ? "catalog1" : "catalog0", &s_disk[i], &size) != ESP_OK ||
                size != sizeof(catalog_t) || !valid_catalog(&s_disk[i]))
                memset(&s_disk[i], 0, sizeof(catalog_t));
            if (s_disk[i].version && s_disk[i].generation >= s_cat.generation)
                s_cat = s_disk[i];
        }
        if (!s_cat.count) migrate_single_photo(nvs);
        nvs_close(nvs);
    }
    const char *legacy[] = {"/storage/current.jpg", "/storage/upload.tmp"};
    for (size_t i = 0; i < sizeof(legacy) / sizeof(legacy[0]); ++i) {
        if (unlink(legacy[i]) != 0 && errno != ENOENT)
            ESP_LOGW("photo_storage", "Legacy cache cleanup failed: %d", errno);
    }
    ESP_LOGI("photo_storage", "Restored %u cached photos; main stack high-water=%u",
             (unsigned)s_cat.count, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return carousel_settings_init();
}
bool photo_storage_has_photo(void){return s_cat.count>0;}
const char *photo_storage_cloud_id(void){return s_cat.count?s_cat.entries[0].id:"";}
const char *photo_storage_active_lvgl_path(void){static char path[64];return photo_storage_snapshot(path,sizeof(path),false)?path:NULL;}
bool photo_storage_snapshot(char *out,size_t n,bool advance){xSemaphoreTake(s_lock,portMAX_DELAY);if(advance&&s_cat.count>1)s_index=(s_index+1)%s_cat.count;bool ok=s_cat.count>0;if(ok){snprintf(out,n,"S:/%s.jpg",s_cat.entries[s_index].id);strlcpy(s_previous,s_shown,sizeof(s_previous));strlcpy(s_shown,s_cat.entries[s_index].id,sizeof(s_shown));}xSemaphoreGive(s_lock);return ok;}
unsigned photo_storage_count(void){xSemaphoreTake(s_lock,portMAX_DELAY);unsigned n=s_cat.count;xSemaphoreGive(s_lock);return n;}
bool photo_storage_contains(const char *id){xSemaphoreTake(s_lock,portMAX_DELAY);bool found=false;for(unsigned i=0;i<s_cat.count;i++)if(!strcmp(id,s_cat.entries[i].id))found=true;xSemaphoreGive(s_lock);return found;}
/* Keep both recovery catalogs and the decoder's current source. Only worker deletes. */
static size_t managed_bytes(void){DIR *d=opendir("/storage");if(!d)return BUDGET;size_t total=0;struct dirent *e;while((e=readdir(d))){size_t len=strlen(e->d_name);if(len!=40||strcmp(e->d_name+36,".jpg"))continue;char id[37];memcpy(id,e->d_name,36);id[36]=0;if(!valid_id(id))continue;bool keep=false;xSemaphoreTake(s_lock,portMAX_DELAY);keep=!strcmp(id,s_shown)||!strcmp(id,s_previous);xSemaphoreGive(s_lock);for(unsigned k=0;k<2;k++)for(unsigned j=0;j<s_disk[k].count;j++)if(!strcmp(id,s_disk[k].entries[j].id))keep=true;char path[64];path_for(id,path,sizeof(path));if(!keep)unlink(path);struct stat st;if(!stat(path,&st))total+=st.st_size;}closedir(d);for(int i=0;i<2;i++){char path[32];struct stat st;snprintf(path,sizeof(path),"/storage/cloud%d.jpg",i);if(!stat(path,&st))total+=st.st_size;}return total;}
bool photo_storage_prepare_download(size_t bytes)
{
    size_t needed = bytes ? bytes : PHOTO_STORAGE_MAX_BYTES;
    uint64_t total = 0, free_bytes = 0;
    if (needed > PHOTO_STORAGE_MAX_BYTES || managed_bytes() + needed > BUDGET)
        return false;
    return esp_vfs_fat_info(PHOTO_STORAGE_MOUNT_POINT, &total, &free_bytes) == ESP_OK &&
           free_bytes >= needed + 64U * 1024U;
}
bool photo_storage_reconcile(const char ids[][37],unsigned count){catalog_t next={0};for(unsigned i=0;i<count&&next.count<LIMIT;i++)for(unsigned j=0;j<s_cat.count;j++)if(!strcmp(ids[i],s_cat.entries[j].id))next.entries[next.count++]=s_cat.entries[j];if(!next.count)return true;if(next.count==s_cat.count&&!memcmp(next.entries,s_cat.entries,sizeof(next.entries)))return true;return publish(&next);}
/* Full software decode into a small block scratch buffer before accepting cloud data. */
static bool validate_cloud_jpeg(const char *path)
{
    struct stat st;
    if (stat(path, &st) || st.st_size < 4 || st.st_size > PHOTO_STORAGE_MAX_BYTES) return false;
    uint8_t *input = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!input) return false;
    FILE *file = fopen(path, "rb");
    bool ok = file && fread(input, 1, st.st_size, file) == (size_t)st.st_size;
    if (file) fclose(file);
    if (!ok || input[0] != 0xff || input[1] != 0xd8 || input[st.st_size-2] != 0xff || input[st.st_size-1] != 0xd9) { free(input); return false; }
    jpeg_dec_config_t config = {.output_type = JPEG_PIXEL_FORMAT_RGB565_LE, .rotate = JPEG_ROTATE_0D, .block_enable = true};
    jpeg_dec_handle_t decoder = NULL;
    jpeg_dec_io_t io = {.inbuf = input, .inbuf_len = st.st_size};
    jpeg_dec_header_info_t header = {0};
    int bytes = 0, count = 0, rows = 0;
    uint8_t *scratch = NULL;
    ok = jpeg_dec_open(&config, &decoder) == JPEG_ERR_OK;
    if (ok) ok = jpeg_dec_parse_header(decoder, &io, &header) == JPEG_ERR_OK && header.width == 1024 && header.height == 600;
    if (ok) ok = jpeg_dec_get_outbuf_len(decoder, &bytes) == JPEG_ERR_OK && bytes > 0 && bytes <= 1024*32*2 &&
                 jpeg_dec_get_process_count(decoder, &count) == JPEG_ERR_OK && count > 0 && count <= 75;
    if (ok) { scratch = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); ok = scratch != NULL; }
    io.outbuf = scratch;
    for (int i = 0; ok && i < count; ++i) {
        ok = jpeg_dec_process(decoder, &io) == JPEG_ERR_OK && io.out_size > 0 && io.out_size <= bytes && io.out_size % 2048 == 0;
        if (ok) rows += io.out_size / 2048;
    }
    ok = ok && rows >= 600 && rows <= 608;
    if (decoder) jpeg_dec_close(decoder);
    free(scratch); free(input);
    return ok;
}


bool photo_storage_commit_cloud(const char *id)
{
    if (!valid_id(id) || photo_storage_contains(id)) return false;
    if (!validate_cloud_jpeg(PHOTO_STORAGE_TEMP_PATH)) {
        ESP_LOGW("photo_storage", "Downloaded JPEG validation failed");
        return false;
    }
    struct stat st;
    if (stat(PHOTO_STORAGE_TEMP_PATH, &st)) return false;
    catalog_t next = s_cat;
    if (next.count >= LIMIT) return false;
    uint32_t total = st.st_size;
    for (unsigned i = 0; i < next.count; ++i) total += next.entries[i].bytes;
    if (total > BUDGET) return false;
    char path[64];
    path_for(id, path, sizeof(path));
    if (rename(PHOTO_STORAGE_TEMP_PATH, path)) {
        ESP_LOGW("photo_storage", "Photo file commit failed: errno=%d", errno);
        return false;
    }
    strlcpy(next.entries[next.count].id, id, 37);
    next.entries[next.count++].bytes = st.st_size;
    if (!publish(&next)) {
        ESP_LOGW("photo_storage", "Photo catalog commit failed");
        return false;
    }
    return true;
}
FILE *photo_storage_open_upload(void){unlink(PHOTO_STORAGE_TEMP_PATH);return fopen(PHOTO_STORAGE_TEMP_PATH,"wb");}
void photo_storage_discard_upload(void){unlink(PHOTO_STORAGE_TEMP_PATH);}

bool photo_storage_decode_frame(const char *lvgl_path, void *pixels, size_t capacity, bool *dirty)
{
    *dirty = false;
    if (!lvgl_path || strlen(lvgl_path) != 43 || strncmp(lvgl_path, "S:/", 3) ||
        strcmp(lvgl_path + 39, ".jpg") || capacity < 1024U * 600U * 2U) return false;
    char id[37], path[64];
    memcpy(id, lvgl_path + 3, 36); id[36] = 0;
    if (!valid_id(id)) return false;
    path_for(id, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) || st.st_size < 4 || st.st_size > PHOTO_STORAGE_MAX_BYTES) return false;
    uint8_t *input = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!input) return false;
    FILE *file = fopen(path, "rb");
    bool ok = file && fread(input, 1, st.st_size, file) == (size_t)st.st_size;
    if (file) fclose(file);
    jpeg_dec_handle_t decoder = NULL;
    jpeg_dec_config_t config = {.output_type=JPEG_PIXEL_FORMAT_RGB565_LE,
                                .rotate=JPEG_ROTATE_0D, .block_enable=false};
    jpeg_dec_io_t io = {.inbuf=input, .inbuf_len=st.st_size, .outbuf=pixels};
    jpeg_dec_header_info_t header = {0};
    int required = 0;
    if (ok) ok = jpeg_dec_open(&config, &decoder) == JPEG_ERR_OK;
    if (ok) ok = jpeg_dec_parse_header(decoder, &io, &header) == JPEG_ERR_OK &&
                 header.width == 1024 && header.height == 600 &&
                 jpeg_dec_get_outbuf_len(decoder, &required) == JPEG_ERR_OK &&
                 required > 0 && (size_t)required <= capacity;
    if (ok) {
        *dirty = true;
        ok = jpeg_dec_process(decoder, &io) == JPEG_ERR_OK;
    }
    if (decoder) jpeg_dec_close(decoder);
    free(input);
    return ok;
}
