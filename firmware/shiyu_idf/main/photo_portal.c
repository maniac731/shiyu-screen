#include "photo_portal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "photo_storage.h"

#define PORTAL_SSID "ShiYu-Screen-A02"
#define PORTAL_PASSWORD "shiyu2026"
#define UPLOAD_BUFFER_BYTES 2048

static const char *TAG = "photo_portal";
static httpd_handle_t s_server;
static photo_portal_state_cb_t s_callback;
static void *s_callback_context;
static bool s_active;
static bool s_netif_ready;
static bool s_event_loop_ready;
static bool s_nvs_ready;
static bool s_wifi_ready;
static esp_netif_t *s_ap_netif;
typedef enum {
    PORTAL_CORE_UNINITIALIZED,
    PORTAL_CORE_READY,
    PORTAL_CORE_FAILED,
} portal_core_state_t;
static portal_core_state_t s_core_state;
static esp_err_t s_core_error = ESP_FAIL;

static const char s_upload_page[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<title>食愈屏幕传图</title><style>"
    ":root{font-family:-apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif;color:#26372f;background:#f6f1e8}"
    "body{margin:0}main{max-width:620px;margin:auto;padding:24px 16px 40px}.card{background:#fffdfc;border-radius:22px;padding:20px}"
    "h1{margin:0 0 8px;font-size:26px}p{color:#708078;line-height:1.6}canvas{display:block;width:100%;aspect-ratio:1024/600;background:#26372f;border-radius:14px}"
    "input{display:none}button,label{box-sizing:border-box;display:block;width:100%;border:0;border-radius:14px;padding:15px;margin-top:12px;text-align:center;font-size:17px;font-weight:650}"
    ".choose{background:#e5eee7}.primary{background:#39734f;color:white}.finish{background:#26372f;color:white}.status{min-height:26px;margin:14px;text-align:center;color:#39734f}"
    "</style></head><body><main><section class='card'><h1>发送照片到食愈屏幕</h1>"
    "<p>照片会在手机上裁成屏幕比例并压缩，完成后保存在设备中。</p>"
    "<canvas id='c' width='1024' height='600'></canvas><label class='choose' for='f'>从相册选择照片</label>"
    "<input id='f' type='file' accept='image/*'><button class='primary' id='send' disabled>发送到屏幕</button>"
    "<div class='status' id='s'>等待选择照片</div><button class='finish' id='finish'>完成并关闭传图</button>"
    "</section></main><script>const W=1024,H=600,c=document.querySelector('#c'),x=c.getContext('2d'),f=document.querySelector('#f'),send=document.querySelector('#send'),s=document.querySelector('#s');let ready=false;"
    "x.fillStyle='#26372f';x.fillRect(0,0,W,H);f.onchange=()=>{const q=f.files[0];if(!q)return;const i=new Image();i.onload=()=>{const z=Math.max(W/i.width,H/i.height),w=i.width*z,h=i.height*z;x.drawImage(i,(W-w)/2,(H-h)/2,w,h);URL.revokeObjectURL(i.src);ready=true;send.disabled=false;s.textContent='预览完成'};i.src=URL.createObjectURL(q)};"
    "send.onclick=()=>{if(!ready)return;send.disabled=true;s.textContent='正在压缩';c.toBlob(b=>{const q=new XMLHttpRequest();q.open('POST','/upload');q.setRequestHeader('Content-Type','image/jpeg');q.upload.onprogress=e=>{if(e.lengthComputable)s.textContent='正在发送 '+Math.round(e.loaded/e.total*100)+'%'};q.onload=()=>{s.textContent=q.status===200?'照片已保存':'失败：'+q.responseText;send.disabled=false};q.onerror=()=>{s.textContent='连接中断';send.disabled=false};q.send(b)},'image/jpeg',.82)};"
    "document.querySelector('#finish').onclick=async()=>{s.textContent='正在关闭';await fetch('/finish',{method:'POST'});document.body.innerHTML='<main><section class=card><h1>传图完成</h1><p>手机可以切回原来的 Wi-Fi。</p></section></main>'};"
    "</script></body></html>";

static void publish_state(photo_portal_state_t state)
{
    if (s_callback != NULL) s_callback(state, s_callback_context);
}

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "Memory %s: internal=%u largest=%u psram=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static esp_err_t fail_core_init(esp_err_t error)
{
    s_core_state = PORTAL_CORE_FAILED;
    s_core_error = error;
    ESP_LOGE(TAG, "Portal core locked unavailable for this boot: %s",
             esp_err_to_name(error));
    return error;
}

static esp_err_t root_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
    return httpd_resp_send(request, s_upload_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t legacy_upload_start_handler(httpd_req_t *request)
{
    httpd_resp_set_status(request, "409 Conflict");
    return httpd_resp_sendstr(request, "上传页面已更新，请关闭网页后重新打开 192.168.4.1");
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > PHOTO_STORAGE_MAX_BYTES) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "图片过大或为空");
        return ESP_FAIL;
    }

    char *buffer = heap_caps_malloc(UPLOAD_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        publish_state(PHOTO_PORTAL_ERROR);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "接收内存不足");
        return ESP_FAIL;
    }

    FILE *file = photo_storage_open_upload();
    if (file == NULL) {
        heap_caps_free(buffer);
        publish_state(PHOTO_PORTAL_ERROR);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "无法写入存储");
        return ESP_FAIL;
    }

    publish_state(PHOTO_PORTAL_RECEIVING);
    ESP_LOGI(TAG, "Receiving JPEG: %d bytes", request->content_len);
    size_t remaining = request->content_len;
    size_t received_total = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(request, buffer,
                                      remaining < UPLOAD_BUFFER_BYTES ? remaining : UPLOAD_BUFFER_BYTES);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0 || fwrite(buffer, 1, received, file) != (size_t)received) {
            fclose(file);
            heap_caps_free(buffer);
            photo_storage_discard_upload();
            publish_state(PHOTO_PORTAL_ERROR);
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "接收中断");
            return ESP_FAIL;
        }
        received_total += received;
        remaining -= received;
    }
    fclose(file);
    heap_caps_free(buffer);

    if (!photo_storage_accept_upload(received_total)) {
        photo_storage_discard_upload();
        publish_state(PHOTO_PORTAL_ERROR);
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "图片格式不正确");
        return ESP_FAIL;
    }
    publish_state(PHOTO_PORTAL_SAVED);
    return httpd_resp_sendstr(request, "OK");
}

static esp_err_t finish_handler(httpd_req_t *request)
{
    httpd_resp_sendstr(request, "OK");
    return ESP_OK;
}

static esp_err_t not_found_handler(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_sendstr(request, "Open Shi Yu upload portal");
}

esp_err_t photo_portal_init(photo_portal_state_cb_t callback, void *context)
{
    s_callback = callback;
    s_callback_context = context;

    if (s_core_state == PORTAL_CORE_READY) return ESP_OK;
    if (s_core_state == PORTAL_CORE_FAILED) return s_core_error;

    esp_err_t result;
    if (!s_netif_ready) {
        result = esp_netif_init();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Network interface init failed: %s", esp_err_to_name(result));
            return fail_core_init(result);
        }
        s_netif_ready = true;
    }

    if (!s_event_loop_ready) {
        result = esp_event_loop_create_default();
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Event loop init failed: %s", esp_err_to_name(result));
            return fail_core_init(result);
        }
        s_event_loop_ready = true;
    }

    if (!s_nvs_ready) {
        result = nvs_flash_init();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "NVS init failed without erase: %s", esp_err_to_name(result));
            return fail_core_init(result);
        }
        s_nvs_ready = true;
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "Unable to create Wi-Fi AP network interface");
            return fail_core_init(ESP_ERR_NO_MEM);
        }
    }

    log_memory("before-wifi-init");
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_init);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed without reboot: %s", esp_err_to_name(result));
        log_memory("wifi-init-failed");
        return fail_core_init(result);
    }
    s_wifi_ready = true;
    s_core_state = PORTAL_CORE_READY;
    log_memory("after-wifi-init");
    return ESP_OK;
}

static esp_err_t register_http_handlers(void)
{
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    const httpd_uri_t legacy_upload_start = {
        .uri = "/upload-start", .method = HTTP_POST, .handler = legacy_upload_start_handler};
    const httpd_uri_t upload = {
        .uri = "/upload", .method = HTTP_POST, .handler = upload_handler};
    const httpd_uri_t finish = {
        .uri = "/finish", .method = HTTP_POST, .handler = finish_handler};
    esp_err_t result = httpd_register_uri_handler(s_server, &root);
    if (result == ESP_OK) result = httpd_register_uri_handler(s_server, &legacy_upload_start);
    if (result == ESP_OK) result = httpd_register_uri_handler(s_server, &upload);
    if (result == ESP_OK) result = httpd_register_uri_handler(s_server, &finish);
    if (result == ESP_OK) {
        result = httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, not_found_handler);
    }
    return result;
}

esp_err_t photo_portal_start(void)
{
    if (s_active) return ESP_OK;
    esp_err_t init_result = photo_portal_init(s_callback, s_callback_context);
    if (init_result != ESP_OK) return init_result;

    wifi_config_t config = {0};
    strlcpy((char *)config.ap.ssid, PORTAL_SSID, sizeof(config.ap.ssid));
    strlcpy((char *)config.ap.password, PORTAL_PASSWORD, sizeof(config.ap.password));
    config.ap.ssid_len = strlen(PORTAL_SSID);
    config.ap.channel = 1;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "configure AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");

    if (s_server == NULL) {
        httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
        server_config.stack_size = 7 * 1024;
        server_config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        server_config.lru_purge_enable = true;
        server_config.recv_wait_timeout = 15;
        server_config.send_wait_timeout = 15;
        ESP_LOGI(TAG, "Starting HTTP server; internal heap free=%u, largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        esp_err_t server_result = httpd_start(&s_server, &server_config);
        if (server_result != ESP_OK) {
            ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(server_result));
            esp_wifi_stop();
            return server_result;
        }
        esp_err_t handler_result = register_http_handlers();
        if (handler_result != ESP_OK) {
            ESP_LOGE(TAG, "HTTP handler registration failed: %s",
                     esp_err_to_name(handler_result));
            httpd_stop(s_server);
            s_server = NULL;
            esp_wifi_stop();
            return handler_result;
        }
    }
    s_active = true;
    publish_state(PHOTO_PORTAL_WAITING);
    log_memory("portal-active");
    ESP_LOGI(TAG, "Portal ready");
    return ESP_OK;
}

void photo_portal_stop(void)
{
    if (!s_active) return;
    if (s_server != NULL) {
        esp_err_t server_result = httpd_stop(s_server);
        if (server_result != ESP_OK) {
            ESP_LOGE(TAG, "HTTP server stop failed: %s", esp_err_to_name(server_result));
        }
        s_server = NULL;
    }
    esp_wifi_stop();
    s_active = false;
    publish_state(PHOTO_PORTAL_STOPPED);
    log_memory("portal-stopped");
    ESP_LOGI(TAG, "Portal Wi-Fi stopped");
}
