#include "cloud_sync.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_lv_adapter.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "cJSON.h"
#include "photo_storage.h"
#include "inventory_model.h"
#include "ui_controller.h"

#define API "https://lkewydexsjjntydpbzri.supabase.co/functions/v1/device-api"
#define STORAGE_PREFIX "https://lkewydexsjjntydpbzri.supabase.co/storage/v1/object/sign/kitchen-photos/"
#define BIND_PREFIX "https://kitchen.maniac3221225477.sbs/#device-bind="
typedef struct { uint32_t version; char id[37]; char secret[65]; } credentials_t;
static credentials_t s_credentials;
static TaskHandle_t s_task;
static void memory(const char *stage)
{
    ESP_LOGI("cloud_sync", "Memory %s internal=%u largest=%u psram=%u psram_largest=%u", stage,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
static void status(const char *text, const char *url)
{
    static char previous[96];
    if (strcmp(previous, text)) {
        ESP_LOGI("cloud_sync", "State: %s", text);
        strlcpy(previous, text, sizeof(previous));
    }
    if (esp_lv_adapter_lock(1000) == ESP_OK) {
        shiyu_ui_set_cloud_state(text, url);
        esp_lv_adapter_unlock();
    }
}
void cloud_sync_request_pairing(void) { if (s_task) xTaskNotifyGive(s_task); }

static bool online(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    return netif && esp_netif_is_netif_up(netif) && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr;
}
static esp_http_client_handle_t client(const char *url)
{
    /* IDF builds path + signed query + protocol in one TX request line. */
    size_t tx_bytes = strlen(url) + 32;
    if (tx_bytes > 4096) {
        ESP_LOGW("cloud_sync", "Request URL exceeds bounded TX capacity");
        return NULL;
    }
    if (tx_bytes < 512) tx_bytes = 512;
    esp_http_client_config_t config = {.url=url,.crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=15000,
        .disable_auto_redirect=true,.buffer_size=1024,.buffer_size_tx=(int)tx_bytes};
    return esp_http_client_init(&config);
}
static cJSON *request_photo(const char *action, const char *photo_id, int *http_status)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"action\":\"%s\",\"%s\":\"%s\"}", action,
             !strcmp(action,"inventory")?"known_revision":"photo_id", photo_id ? photo_id : "");
    esp_http_client_handle_t http = client(API);
    if (!http) return NULL;
    esp_http_client_set_method(http, HTTP_METHOD_POST);
    esp_http_client_set_header(http, "Content-Type", "application/json");
    esp_http_client_set_header(http, "X-Device-Id", s_credentials.id);
    esp_http_client_set_header(http, "X-Device-Secret", s_credentials.secret);
    const int limit = !strcmp(action,"inventory") ? INVENTORY_RESPONSE_MAX : 4096;
    char *response = heap_caps_malloc(limit+1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memory("before-api");
    cJSON *json = NULL;
    if (response && esp_http_client_open(http, strlen(body)) == ESP_OK &&
        esp_http_client_write(http, body, strlen(body)) == (int)strlen(body) && esp_http_client_fetch_headers(http) >= 0) {
        *http_status = esp_http_client_get_status_code(http);
        int used=0, n=0;
        while (used < limit && (n=esp_http_client_read(http,response+used,limit-used)) > 0) used+=n;
        if (*http_status == 200 && used < limit && n >= 0 && esp_http_client_is_complete_data_received(http)) {
            response[used]=0; json=cJSON_Parse(response);
        }
    }
    free(response); esp_http_client_close(http); esp_http_client_cleanup(http);
    memory("after-api");
    return json;
}
static cJSON *request(const char *action,int *code){return request_photo(action,NULL,code);}
static const char *string(cJSON *json, const char *key)
{
    cJSON *value=cJSON_GetObjectItemCaseSensitive(json,key);
    return cJSON_IsString(value) ? value->valuestring : "";
}
static bool download(const char *url)
{
    if (strncmp(url, STORAGE_PREFIX, strlen(STORAGE_PREFIX))) return false;
    esp_http_client_handle_t http=client(url);
    if(!http) return false;
    FILE *file=NULL; bool ok=false; size_t total=0;
    char *chunk=heap_caps_malloc(2048,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(chunk && esp_http_client_open(http,0)==ESP_OK && esp_http_client_fetch_headers(http)>=0 &&
       esp_http_client_get_status_code(http)==200 && esp_http_client_get_content_length(http)<=(int64_t)PHOTO_STORAGE_MAX_BYTES) {
        file=photo_storage_open_upload();
        if(file) {
            int n;
            while((n=esp_http_client_read(http,chunk,2048))>0) {
                total+=n;
                if(total>PHOTO_STORAGE_MAX_BYTES || fwrite(chunk,1,n,file)!=(size_t)n) break;
            }
            ok=n==0 && total>0 && total<=PHOTO_STORAGE_MAX_BYTES && esp_http_client_is_complete_data_received(http);
            if (fflush(file)) ok=false;
            if (fclose(file)) ok=false;
        }
    }
    free(chunk);esp_http_client_close(http);esp_http_client_cleanup(http);
    if(!ok) photo_storage_discard_upload();
    return ok;
}

static void worker(void *arg)
{
    (void)arg; bool enrolled=false, revoked=false, want_pair=false; time_t pairing_deadline=0; char binding_url[256]="";
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL); esp_sntp_setservername(0,"pool.ntp.org"); esp_sntp_init();
    while(true) {
        bool pair_requested=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(15000))>0;
        want_pair = want_pair || pair_requested;
        if (binding_url[0] && time(NULL) >= pairing_deadline) {
            binding_url[0] = 0;
            want_pair = true;
            status("QR expired - waiting for refresh",NULL);
        }
        if(revoked){inventory_model_connection(false,false);status("Device disabled",NULL);continue;}
        if(!online()){inventory_model_connection(false,true);status("Waiting for Wi-Fi",NULL);continue;}
        if(time(NULL)<1700000000){status("Waiting for secure clock",NULL);continue;}
        int code=0;
        if(!enrolled){cJSON *result=request("enroll",&code);if(!result){status("Cloud unavailable - retrying",NULL);continue;}enrolled=true;cJSON_Delete(result);}
        cJSON *state=request("status",&code);
        if(!state){if(code==403){revoked=true;status("Device disabled",NULL);}else status("Cloud unavailable - retrying",NULL);continue;}
        bool bound=!strcmp(string(state,"state"),"bound");cJSON_Delete(state);
        if(!bound){
            inventory_model_connection(false,false);
            if(want_pair) {
                binding_url[0] = 0;
                status("Preparing fresh QR",NULL);
                cJSON *pair=request("pairing",&code);
                if(pair){const char *url=string(pair,"bind_url");
                    if(!strncmp(url,BIND_PREFIX,strlen(BIND_PREFIX)) && strlen(url)==strlen(BIND_PREFIX)+64 && strlen(url)<sizeof(binding_url)) {
                        strlcpy(binding_url,url,sizeof(binding_url));pairing_deadline=time(NULL)+590;
                        want_pair=false;
                    }
                    cJSON_Delete(pair);
                }
            }
            status(binding_url[0]?"Scan with kitchen owner phone":want_pair?"Waiting for fresh QR - retrying":"Open Cloud in Settings to bind",binding_url[0]?binding_url:NULL);continue;
        }
        binding_url[0]=0;status("Kitchen bound - syncing",NULL);
        char revision[65]; inventory_model_revision(revision);
        cJSON *stock=request_photo("inventory",revision,&code);
        if(!stock || !inventory_model_accept(stock)) inventory_model_connection(false,true);
        cJSON_Delete(stock);
        cJSON *manifest=request("album",&code);
        if(!manifest){status("Sync unavailable - keeping photos",NULL);continue;}
        cJSON *photos=cJSON_GetObjectItemCaseSensitive(manifest,"photos");
        char ids[20][37]={{0}}; unsigned count=0;
        cJSON *photo;
        cJSON_ArrayForEach(photo,photos){const char *id=string(photo,"id");if(count<20&&strlen(id)==36)strlcpy(ids[count++],id,37);}
        if(count && !photo_storage_reconcile(ids,count)){status("Cache save failed",NULL);cJSON_Delete(manifest);continue;}
        bool capacity_limited=false,download_failed=false;
        cJSON_ArrayForEach(photo,photos){
            const char *id=string(photo,"id");
            if(strlen(id)!=36||photo_storage_contains(id))continue;
            cJSON *size=cJSON_GetObjectItemCaseSensitive(photo,"bytes");
            size_t bytes=cJSON_IsNumber(size)&&size->valuedouble>0?(size_t)size->valuedouble:PHOTO_STORAGE_MAX_BYTES;
            if(bytes>PHOTO_STORAGE_MAX_BYTES||!photo_storage_prepare_download(bytes)){capacity_limited=true;continue;}
            cJSON *signed_photo=request_photo("photo_url",id,&code);
            bool ok=signed_photo&&download(string(signed_photo,"download_url"))&&photo_storage_commit_cloud(id);
            cJSON_Delete(signed_photo);
            if(!ok){download_failed=true;photo_storage_discard_upload();status("Download failed - keeping cache",NULL);continue;}
            if(photo_storage_count()==1&&esp_lv_adapter_lock(-1)==ESP_OK){char path[64];if(photo_storage_snapshot(path,sizeof(path),false))shiyu_ui_set_ambient_photo(path);esp_lv_adapter_unlock();}
        }
        status(capacity_limited?"Cache full - keeping local photos":download_failed?"Some photos failed - retrying":count?"Photos synced":"No cloud photos - keeping cache",NULL);
        cJSON_Delete(manifest);
    }
}
static void *json_alloc(size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
esp_err_t cloud_sync_init(void)
{
    /* Hundreds of small inventory JSON nodes must not consume internal Wi-Fi RAM.
       Configure once before the only JSON/network worker is started. */
    cJSON_Hooks hooks = {.malloc_fn=json_alloc, .free_fn=free};
    cJSON_InitHooks(&hooks);
    nvs_handle_t nvs;esp_err_t err=nvs_open("cloud_device",NVS_READWRITE,&nvs);if(err!=ESP_OK)return err;
    size_t size=sizeof(s_credentials);err=nvs_get_blob(nvs,"identity",&s_credentials,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND) {
        uint8_t random[48];esp_fill_random(random,sizeof(random));
        s_credentials.version=1;random[6]=(random[6]&0x0f)|0x40;random[8]=(random[8]&0x3f)|0x80;
        snprintf(s_credentials.id,sizeof(s_credentials.id),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
          random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15]);
        for(int i=0;i<32;i++)snprintf(s_credentials.secret+i*2,3,"%02x",random[16+i]);
        err=nvs_set_blob(nvs,"identity",&s_credentials,sizeof(s_credentials));if(err==ESP_OK)err=nvs_commit(nvs);
    }else if(err==ESP_OK && (size!=sizeof(s_credentials)||s_credentials.version!=1||s_credentials.id[36]||s_credentials.secret[64]))err=ESP_ERR_INVALID_STATE;
    nvs_close(nvs);if(err!=ESP_OK)return err;
    inventory_model_identity(s_credentials.id);
    return xTaskCreateWithCaps(worker,"cloud_sync",8192,NULL,3,&s_task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
