#include "inventory_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

typedef struct {
    uint32_t schema, generation, count;
    int64_t saved_at;
    char device[37], household[37], revision[65];
    inventory_row_t rows[INVENTORY_LIMIT];
    uint32_t checksum;
} snapshot_t;
static snapshot_t *s_data;
static SemaphoreHandle_t s_lock;
static char s_device[37];
static bool s_available, s_online;
static unsigned s_serial;
static time_t s_contact;
static unsigned s_slot;
static const char *paths[] = {"/storage/stock0.bin", "/storage/stock1.bin"};
static const char *str(const cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static bool hex(const char *s, size_t length)
{
    if (strnlen(s, length + 1) != length) return false;
    for (size_t i=0; i<length; ++i)
        if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
    return true;
}
static bool uuid(const char *s)
{
    if (strnlen(s,37)!=36) return false;
    for (unsigned i=0;i<36;++i) {
        if (i==8||i==13||i==18||i==23) { if(s[i]!='-') return false; }
        else if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
    }
    return true;
}
static bool text(const char *s, size_t max)
{
    size_t n=strnlen(s,max+1);
    if(!n||n>max) return false;
    for(size_t i=0;i<n;++i) if((unsigned char)s[i]<32 || s[i]==127) return false;
    return true;
}
static bool quantity(const char *s)
{
    size_t n=strnlen(s,16); unsigned decimals=0; bool dot=false;
    if(!n||n>=16) return false;
    for(size_t i=0;i<n;++i) {
        if(s[i]=='.' && !dot && i && i+1<n) { dot=true; continue; }
        if(s[i]<'0'||s[i]>'9'||(dot&&++decimals>3)) return false;
    }
    double v=strtod(s,NULL);
    return isfinite(v)&&v>=0&&v<=999999.999;
}
static uint32_t checksum(const snapshot_t *s)
{
    const unsigned char *p=(const void *)s; uint32_t h=2166136261U;
    for(size_t i=0;i<offsetof(snapshot_t,checksum);++i) h=(h^p[i])*16777619U;
    return h;
}
static bool valid(const snapshot_t *s)
{
    if(s->schema!=1 || s->count>INVENTORY_LIMIT || s->checksum!=checksum(s) ||
       !uuid(s->device)||!uuid(s->household)||!hex(s->revision,64)) return false;
    for(unsigned i=0;i<s->count;++i)
        if(!uuid(s->rows[i].id)||!text(s->rows[i].name,120)||
           !text(s->rows[i].unit,24)||!quantity(s->rows[i].quantity)) return false;
    return true;
}
static bool read_slot(unsigned slot, snapshot_t *s)
{
    FILE *f=fopen(paths[slot],"rb");
    if(!f) return false;
    bool ok=fread(s,1,sizeof(*s),f)==sizeof(*s) && fgetc(f)==EOF && valid(s);
    fclose(f); return ok;
}
bool inventory_model_init(void)
{
    s_lock=xSemaphoreCreateMutex();
    s_data=heap_caps_calloc(1,sizeof(*s_data),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    snapshot_t *candidate=heap_caps_malloc(sizeof(*candidate),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_lock||!s_data||!candidate) { free(candidate); return false; }
    uint32_t marker=2; nvs_handle_t n;
    if(nvs_open("inventory",NVS_READONLY,&n)==ESP_OK) { nvs_get_u32(n,"active",&marker); nvs_close(n); }
    bool loaded=false;
    if(marker<2 && read_slot(marker,candidate)) { *s_data=*candidate; s_slot=marker; loaded=true; }
    if(!loaded) for(unsigned i=0;i<2;++i) {
        if(read_slot(i,candidate) && (!loaded||candidate->generation>s_data->generation)) {
            *s_data=*candidate; s_slot=i; loaded=true;
        }
    }
    free(candidate);
    ESP_LOGI("inventory", "Cache restored=%d count=%u",loaded,(unsigned)s_data->count);
    return true;
}
void inventory_model_identity(const char *device)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    strlcpy(s_device,device,sizeof(s_device));
    s_available=s_data->schema==1 && !strcmp(s_data->device,s_device);
    s_serial++;
    xSemaphoreGive(s_lock);
}
void inventory_model_connection(bool online, bool available)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(s_online!=online || (!available&&s_available)) s_serial++;
    s_online=online;
    if(!available) s_available=false;
    xSemaphoreGive(s_lock);
}
void inventory_model_revision(char revision[65])
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    strlcpy(revision,s_available?s_data->revision:"",65);
    xSemaphoreGive(s_lock);
}
bool inventory_model_meta(unsigned *count, bool *online, unsigned *serial)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool known=s_available && s_data->schema==1;
    *count=known?s_data->count:0; *online=s_online && time(NULL)-s_contact<90;
    *serial=s_serial;
    xSemaphoreGive(s_lock); return known;
}
bool inventory_model_row(unsigned index, inventory_row_t *row)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool ok=s_available&&index<s_data->count;
    if(ok) *row=s_data->rows[index];
    xSemaphoreGive(s_lock); return ok;
}
bool inventory_model_accept(const cJSON *json)
{
    const char *household=str(json,"household_id"), *revision=str(json,"revision");
    if(!uuid(household)||!hex(revision,64)) return false;
    if(!strcmp(str(json,"state"),"unchanged")) {
        xSemaphoreTake(s_lock,portMAX_DELAY);
        bool ok=s_available&&!strcmp(household,s_data->household)&&!strcmp(revision,s_data->revision);
        if(ok) { if(!s_online) s_serial++; s_online=true; s_contact=time(NULL); }
        xSemaphoreGive(s_lock); return ok;
    }
    if(strcmp(str(json,"state"),"inventory")) return false;
    const cJSON *items=cJSON_GetObjectItemCaseSensitive(json,"items");
    if(!cJSON_IsArray(items)||cJSON_GetArraySize(items)>INVENTORY_LIMIT) return false;
    snapshot_t *next=heap_caps_calloc(1,sizeof(*next),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!next) return false;
    next->schema=1; next->generation=s_data->generation+1;
    next->saved_at=time(NULL);
    strlcpy(next->device,s_device,sizeof(next->device));
    strlcpy(next->household,household,sizeof(next->household));
    strlcpy(next->revision,revision,sizeof(next->revision));
    const cJSON *entry; bool ok=true;
    cJSON_ArrayForEach(entry,items) {
        const char *id=str(entry,"id"),*name=str(entry,"name"),*qty=str(entry,"quantity"),*unit=str(entry,"unit");
        if(!uuid(id)||!text(name,120)||!quantity(qty)||!text(unit,24)) { ok=false; break; }
        inventory_row_t *r=&next->rows[next->count++];
        strlcpy(r->id,id,sizeof(r->id)); strlcpy(r->name,name,sizeof(r->name));
        strlcpy(r->quantity,qty,sizeof(r->quantity)); strlcpy(r->unit,unit,sizeof(r->unit));
    }
    next->checksum=checksum(next);
    unsigned slot=1-s_slot;
    if(ok) {
        FILE *f=fopen(paths[slot],"wb");
        ok=f && fwrite(next,1,sizeof(*next),f)==sizeof(*next);
        if(f) { if(fflush(f)) ok=false; if(fclose(f)) ok=false; }
    }
    if(ok) {
        nvs_handle_t n; ok=nvs_open("inventory",NVS_READWRITE,&n)==ESP_OK;
        if(ok) { esp_err_t e=nvs_set_u32(n,"active",slot); if(e==ESP_OK)e=nvs_commit(n); nvs_close(n); ok=e==ESP_OK; }
    }
    if(ok) {
        xSemaphoreTake(s_lock,portMAX_DELAY);
        snapshot_t *old=s_data; s_data=next; s_slot=slot;
        s_available=true; s_online=true; s_contact=time(NULL); s_serial++;
        xSemaphoreGive(s_lock); free(old);
        ESP_LOGI("inventory","Snapshot committed count=%u",(unsigned)next->count);
    } else free(next);
    return ok;
}
unsigned inventory_model_page(unsigned start, inventory_row_t *rows, unsigned capacity, unsigned *total)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    *total=s_available?s_data->count:0;
    unsigned count=start<*total?*total-start:0;
    if(count>capacity) count=capacity;
    if(count) memcpy(rows,&s_data->rows[start],count*sizeof(*rows));
    xSemaphoreGive(s_lock); return count;
}
bool inventory_model_find(const char *id, inventory_row_t *row)
{
    xSemaphoreTake(s_lock,portMAX_DELAY); bool found=false;
    for(unsigned i=0;s_available&&i<s_data->count;++i) if(!strcmp(id,s_data->rows[i].id)) {
        *row=s_data->rows[i]; found=true; break;
    }
    xSemaphoreGive(s_lock); return found;
}
