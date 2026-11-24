#include "wifi_manager.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_NAMESPACE "wifi_cfg"
#define WIFI_KEY_SSID "ssid"
#define WIFI_KEY_PASSWORD "password"
#define WIFI_COMMAND_QUEUE_LENGTH 8
#define WIFI_WORKER_STACK_BYTES (6 * 1024)
#define WIFI_PROVISIONING_TIMEOUT_US (5ULL * 60ULL * 1000000ULL)
#define WIFI_MAX_CANDIDATE_ATTEMPTS 3
#define WIFI_MAX_SCAN_RESULTS 10

typedef enum {
    COMMAND_START,
    COMMAND_START_SCAN,
    COMMAND_SCAN_DONE,
    COMMAND_START_PROVISIONING,
    COMMAND_STOP_PROVISIONING,
    COMMAND_CONNECT_CANDIDATE,
    COMMAND_STA_DISCONNECTED,
    COMMAND_GOT_IP,
    COMMAND_RECONNECT,
    COMMAND_RETRY_NOW,
    COMMAND_FORGET_NETWORK,
} command_type_t;

typedef struct {
    command_type_t type;
    uint8_t ssid[32];
    uint8_t ssid_len;
    char password[65];
} wifi_command_t;

typedef enum {
    RADIO_STOPPED,
    RADIO_STA,
    RADIO_SCANNING,
    RADIO_PROVISIONING_AP,
} radio_mode_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    char password[65];
    bool valid;
} credentials_t;

static const char *TAG = "wifi_manager";
static QueueHandle_t s_command_queue;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_provisioning_timer;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_provisioning_server;
static wifi_manager_status_cb_t s_status_callback;
static wifi_manager_scan_cb_t s_scan_callback;
static void *s_status_context;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static credentials_t s_saved_credentials;
static credentials_t s_active_credentials;
static bool s_candidate_pending;
static unsigned s_candidate_attempts;
static unsigned s_retry_attempts;
static bool s_wifi_started;
static radio_mode_t s_radio_mode;
static char s_setup_ssid[33];
static char s_setup_password[16];

static void stop_provisioning_server(void);

static void make_display_ssid(const uint8_t *raw, size_t raw_len,
                              char *display, size_t display_size);

static const char s_setup_page[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>食愈网络配置</title><style>"
    ":root{font-family:-apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif;color:#26372f;background:#f6f1e8}"
    "body{margin:0}main{max-width:560px;margin:auto;padding:28px 16px}.card{background:#fffdfc;border-radius:22px;padding:22px}"
    "h1{margin:0 0 10px;font-size:26px}p{color:#708078;line-height:1.6}label{display:block;margin-top:16px;font-weight:650}"
    "input{box-sizing:border-box;width:100%;margin-top:8px;padding:14px;border:1px solid #cad6ce;border-radius:12px;font-size:17px}"
    "button{width:100%;border:0;border-radius:14px;padding:15px;margin-top:20px;background:#39734f;color:white;font-size:17px;font-weight:650}"
    "</style></head><body><main><section class='card'><h1>连接食愈屏幕</h1>"
    "<p>请输入家庭 Wi-Fi 或手机热点。仅支持 2.4GHz 网络。</p>"
    "<form method='post' action='/configure'><label>网络名称<input name='ssid' maxlength='32' required></label>"
    "<label>密码<input name='password' type='password' minlength='8' maxlength='64' required></label>"
    "<button type='submit'>连接网络</button></form></section></main></body></html>";

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "Memory %s: internal=%u largest=%u psram=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static size_t utf8_sequence_length(uint8_t value)
{
    if (value < 0x80) return 1;
    if ((value & 0xE0) == 0xC0) return 2;
    if ((value & 0xF0) == 0xE0) return 3;
    if ((value & 0xF8) == 0xF0) return 4;
    return 0;
}

static void make_display_ssid(const uint8_t *raw, size_t raw_len,
                              char *display, size_t display_size)
{
    if (display == NULL || display_size == 0) return;
    size_t input = 0;
    size_t output = 0;
    bool truncated = false;
    while (input < raw_len) {
        size_t sequence = utf8_sequence_length(raw[input]);
        bool valid = sequence > 0 && input + sequence <= raw_len;
        for (size_t index = 1; valid && index < sequence; ++index) {
            valid = (raw[input + index] & 0xC0) == 0x80;
        }
        if (!valid) {
            if (output + 1 >= display_size) {
                truncated = true;
                break;
            }
            display[output++] = '?';
            ++input;
            continue;
        }
        if (output + sequence >= display_size) {
            truncated = true;
            break;
        }
        memcpy(display + output, raw + input, sequence);
        output += sequence;
        input += sequence;
    }
    if (truncated && output + 3 < display_size) {
        memcpy(display + output, "…", 3);
        output += 3;
    }
    display[output] = '\0';
}

static void publish_status_raw(wifi_manager_state_t state,
                               const uint8_t *ssid, size_t ssid_len)
{
    wifi_manager_status_t status = {.state = state, .ssid = {0}};
    if (ssid != NULL && ssid_len > 0) {
        make_display_ssid(ssid, ssid_len, status.ssid, sizeof(status.ssid));
    }
    if (s_status_callback != NULL) s_status_callback(&status, s_status_context);
}

static void publish_status(wifi_manager_state_t state, const credentials_t *credentials)
{
    publish_status_raw(state,
                       credentials != NULL ? credentials->ssid : NULL,
                       credentials != NULL ? credentials->ssid_len : 0);
}

static bool post_command(const wifi_command_t *command)
{
    return s_command_queue != NULL &&
           xQueueSend(s_command_queue, command, 0) == pdTRUE;
}

static void timer_reconnect(void *context)
{
    (void)context;
    const wifi_command_t command = {.type = COMMAND_RECONNECT};
    post_command(&command);
}

static void timer_stop_provisioning(void *context)
{
    (void)context;
    const wifi_command_t command = {.type = COMMAND_STOP_PROVISIONING};
    post_command(&command);
}

static esp_err_t load_credentials(credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (result != ESP_OK) return result;

    size_t ssid_size = sizeof(credentials->ssid);
    size_t password_size = sizeof(credentials->password);
    result = nvs_get_blob(handle, WIFI_KEY_SSID, credentials->ssid, &ssid_size);
    if (result == ESP_ERR_NVS_TYPE_MISMATCH) {
        char legacy_ssid[33] = {0};
        ssid_size = sizeof(legacy_ssid);
        result = nvs_get_str(handle, WIFI_KEY_SSID, legacy_ssid, &ssid_size);
        if (result == ESP_OK) {
            credentials->ssid_len = strnlen(legacy_ssid, sizeof(legacy_ssid) - 1);
            memcpy(credentials->ssid, legacy_ssid, credentials->ssid_len);
        }
    } else if (result == ESP_OK) {
        credentials->ssid_len = ssid_size;
    }
    if (result == ESP_OK) {
        result = nvs_get_str(handle, WIFI_KEY_PASSWORD, credentials->password,
                             &password_size);
    }
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (result != ESP_OK) return result;
    credentials->valid = credentials->ssid_len > 0;
    return ESP_OK;
}

static esp_err_t save_credentials(const credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, WIFI_KEY_SSID, credentials->ssid,
                          credentials->ssid_len);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, WIFI_KEY_PASSWORD, credentials->password);
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

static esp_err_t erase_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (result != ESP_OK) return result;
    esp_err_t ssid_result = nvs_erase_key(handle, WIFI_KEY_SSID);
    esp_err_t password_result = nvs_erase_key(handle, WIFI_KEY_PASSWORD);
    if (ssid_result == ESP_ERR_NVS_NOT_FOUND) ssid_result = ESP_OK;
    if (password_result == ESP_ERR_NVS_NOT_FOUND) password_result = ESP_OK;
    result = ssid_result != ESP_OK ? ssid_result : password_result;
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

static void stop_reconnect_timer(void)
{
    if (s_reconnect_timer != NULL && esp_timer_is_active(s_reconnect_timer)) {
        esp_timer_stop(s_reconnect_timer);
    }
}

static void schedule_reconnect(unsigned delay_seconds)
{
    stop_reconnect_timer();
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_seconds * 1000000ULL);
}

static void stop_wifi(void)
{
    stop_reconnect_timer();
    s_radio_mode = RADIO_STOPPED;
    if (s_wifi_started) {
        esp_err_t result = esp_wifi_stop();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi stop failed: %s", esp_err_to_name(result));
        }
        s_wifi_started = false;
    }
}

static esp_err_t start_sta(const credentials_t *credentials)
{
    if (credentials == NULL || !credentials->valid) return ESP_ERR_INVALID_ARG;
    /* Every transition out of provisioning must release HTTP and its timeout. */
    stop_provisioning_server();
    stop_wifi();

    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, credentials->ssid_len);
    strlcpy((char *)config.sta.password, credentials->password,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = credentials->password[0] == '\0'
                                         ? WIFI_AUTH_OPEN
                                         : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (result == ESP_OK) result = esp_wifi_start();
    if (result == ESP_OK) {
        s_wifi_started = true;
        s_radio_mode = RADIO_STA;
        result = esp_wifi_connect();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Unable to start station: %s", esp_err_to_name(result));
        stop_wifi();
        return result;
    }
    publish_status(WIFI_MANAGER_CONNECTING, credentials);
    log_memory("station-started");
    return ESP_OK;
}

static void generate_setup_credentials(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_setup_ssid, sizeof(s_setup_ssid), "ShiYu-Setup-%02X%02X", mac[4], mac[5]);
    snprintf(s_setup_password, sizeof(s_setup_password), "sy%08lX",
             (unsigned long)esp_random());
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_form_value(const char *source, size_t source_length,
                              char *destination, size_t destination_size)
{
    size_t output = 0;
    for (size_t index = 0; index < source_length; ++index) {
        char value = source[index];
        if (value == '+') {
            value = ' ';
        } else if (value == '%' && index + 2 < source_length) {
            int high = hex_value(source[index + 1]);
            int low = hex_value(source[index + 2]);
            if (high < 0 || low < 0) return false;
            value = (char)((high << 4) | low);
            index += 2;
        }
        if (output + 1 >= destination_size) return false;
        destination[output++] = value;
    }
    destination[output] = '\0';
    return true;
}

static bool read_form_field(const char *body, const char *name,
                            char *destination, size_t destination_size)
{
    const size_t name_length = strlen(name);
    const char *field = body;
    while (field != NULL && *field != '\0') {
        if (strncmp(field, name, name_length) == 0 && field[name_length] == '=') {
            const char *value = field + name_length + 1;
            const char *end = strchr(value, '&');
            size_t length = end == NULL ? strlen(value) : (size_t)(end - value);
            return decode_form_value(value, length, destination, destination_size);
        }
        field = strchr(field, '&');
        if (field != NULL) ++field;
    }
    return false;
}

static esp_err_t setup_root_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, s_setup_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t setup_configure_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 256) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid form");
    }
    char body[257] = {0};
    int received_total = 0;
    while (received_total < request->content_len) {
        int received = httpd_req_recv(request, body + received_total,
                                      request->content_len - received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "Connection interrupted");
        }
        received_total += received;
    }
    body[received_total] = '\0';

    wifi_command_t command = {.type = COMMAND_CONNECT_CANDIDATE};
    char form_ssid[33] = {0};
    if (!read_form_field(body, "ssid", form_ssid, sizeof(form_ssid)) ||
        form_ssid[0] == '\0' ||
        !read_form_field(body, "password", command.password, sizeof(command.password)) ||
        strlen(command.password) < 8) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid network");
    }
    command.ssid_len = strnlen(form_ssid, sizeof(form_ssid) - 1);
    memcpy(command.ssid, form_ssid, command.ssid_len);

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    esp_err_t result = httpd_resp_sendstr(
        request,
        "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
        "<h2>正在连接</h2><p>请回到食愈屏幕查看连接结果。</p>");
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!post_command(&command)) ESP_LOGE(TAG, "Unable to queue candidate network");
    }
    return result;
}

static esp_err_t start_provisioning_server(void)
{
    if (s_provisioning_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6 * 1024;
    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&s_provisioning_server, &config);
    if (result != ESP_OK) return result;

    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET,
                              .handler = setup_root_handler};
    const httpd_uri_t configure = {.uri = "/configure", .method = HTTP_POST,
                                   .handler = setup_configure_handler};
    result = httpd_register_uri_handler(s_provisioning_server, &root);
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(s_provisioning_server, &configure);
    }
    if (result != ESP_OK) {
        httpd_stop(s_provisioning_server);
        s_provisioning_server = NULL;
    }
    return result;
}

static void stop_provisioning_server(void)
{
    if (s_provisioning_server != NULL) {
        httpd_stop(s_provisioning_server);
        s_provisioning_server = NULL;
    }
    if (s_provisioning_timer != NULL && esp_timer_is_active(s_provisioning_timer)) {
        esp_timer_stop(s_provisioning_timer);
    }
}

static esp_err_t start_provisioning_ap(void)
{
    stop_wifi();
    wifi_config_t config = {0};
    strlcpy((char *)config.ap.ssid, s_setup_ssid, sizeof(config.ap.ssid));
    strlcpy((char *)config.ap.password, s_setup_password, sizeof(config.ap.password));
    config.ap.ssid_len = strlen(s_setup_ssid);
    config.ap.channel = 1;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (result == ESP_OK) result = esp_wifi_start();
    if (result == ESP_OK) {
        s_wifi_started = true;
        s_radio_mode = RADIO_PROVISIONING_AP;
        result = start_provisioning_server();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Unable to start setup access point: %s", esp_err_to_name(result));
        stop_provisioning_server();
        stop_wifi();
        return result;
    }
    esp_timer_start_once(s_provisioning_timer, WIFI_PROVISIONING_TIMEOUT_US);
    publish_status_raw(WIFI_MANAGER_PROVISIONING,
                       (const uint8_t *)s_setup_ssid, strlen(s_setup_ssid));
    log_memory("provisioning-active");
    return ESP_OK;
}

static void schedule_retry(void)
{
    unsigned delay = 1U << (s_retry_attempts < 6 ? s_retry_attempts : 6);
    if (delay > 60) delay = 60;
    ++s_retry_attempts;
    publish_status(WIFI_MANAGER_RETRY_WAIT, &s_active_credentials);
    schedule_reconnect(delay);
}

static void handle_disconnected(void)
{
    if (s_radio_mode != RADIO_STA) return;
    if (s_candidate_pending) {
        ++s_candidate_attempts;
        if (s_candidate_attempts >= WIFI_MAX_CANDIDATE_ATTEMPTS) {
            s_candidate_pending = false;
            publish_status(WIFI_MANAGER_FAILED, &s_active_credentials);
            if (s_saved_credentials.valid) {
                stop_wifi();
                s_active_credentials = s_saved_credentials;
                s_retry_attempts = 0;
                schedule_reconnect(8);
            } else {
                stop_wifi();
            }
            return;
        }
    }
    schedule_retry();
}

static void handle_got_ip(void)
{
    stop_reconnect_timer();
    s_retry_attempts = 0;
    if (s_candidate_pending) {
        esp_err_t result = save_credentials(&s_active_credentials);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Unable to save network credentials: %s", esp_err_to_name(result));
            publish_status(WIFI_MANAGER_FAILED, &s_active_credentials);
            return;
        }
        s_saved_credentials = s_active_credentials;
        s_candidate_pending = false;
    }
    publish_status(WIFI_MANAGER_ONLINE, &s_active_credentials);
    log_memory("station-online");
}

static wifi_manager_auth_t classify_auth(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return WIFI_MANAGER_AUTH_OPEN;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return WIFI_MANAGER_AUTH_PERSONAL;
        default:
            return WIFI_MANAGER_AUTH_UNSUPPORTED;
    }
}

static bool same_ssid(const wifi_manager_scan_item_t *item,
                      const uint8_t *ssid, size_t ssid_len)
{
    return item->ssid_len == ssid_len &&
           memcmp(item->ssid, ssid, ssid_len) == 0;
}

static void sort_scan_items(wifi_manager_scan_item_t *items, size_t count)
{
    for (size_t index = 1; index < count; ++index) {
        wifi_manager_scan_item_t item = items[index];
        size_t position = index;
        while (position > 0 && items[position - 1].rssi < item.rssi) {
            items[position] = items[position - 1];
            --position;
        }
        items[position] = item;
    }
}

static void publish_scan_results(void)
{
    uint16_t record_count = 0;
    esp_err_t result = esp_wifi_scan_get_ap_num(&record_count);
    wifi_ap_record_t *records = NULL;
    if (result == ESP_OK && record_count > 0) {
        records = heap_caps_calloc(record_count, sizeof(*records),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (records == NULL) result = ESP_ERR_NO_MEM;
    }
    if (result == ESP_OK && record_count > 0) {
        result = esp_wifi_scan_get_ap_records(&record_count, records);
    }

    wifi_manager_scan_item_t selected[WIFI_MAX_SCAN_RESULTS] = {0};
    size_t selected_count = 0;
    if (result == ESP_OK) {
        for (uint16_t record_index = 0; record_index < record_count; ++record_index) {
            const wifi_ap_record_t *record = &records[record_index];
            size_t ssid_len = strnlen((const char *)record->ssid, sizeof(record->ssid));
            if (ssid_len == 0) continue;

            size_t duplicate = selected_count;
            for (size_t index = 0; index < selected_count; ++index) {
                if (same_ssid(&selected[index], record->ssid, ssid_len)) {
                    duplicate = index;
                    break;
                }
            }
            if (duplicate < selected_count) {
                if (record->rssi > selected[duplicate].rssi) {
                    selected[duplicate].rssi = record->rssi;
                    selected[duplicate].auth = classify_auth(record->authmode);
                }
                continue;
            }

            size_t target = selected_count;
            if (selected_count < WIFI_MAX_SCAN_RESULTS) {
                ++selected_count;
            } else {
                target = 0;
                for (size_t index = 1; index < selected_count; ++index) {
                    if (selected[index].rssi < selected[target].rssi) target = index;
                }
                if (record->rssi <= selected[target].rssi) continue;
            }
            memset(&selected[target], 0, sizeof(selected[target]));
            selected[target].ssid_len = ssid_len;
            memcpy(selected[target].ssid, record->ssid, ssid_len);
            selected[target].rssi = record->rssi;
            selected[target].auth = classify_auth(record->authmode);
            make_display_ssid(selected[target].ssid, selected[target].ssid_len,
                              selected[target].display_ssid,
                              sizeof(selected[target].display_ssid));
        }
    }
    heap_caps_free(records);
    esp_wifi_clear_ap_list();
    sort_scan_items(selected, selected_count);

    size_t allocation_size = sizeof(wifi_manager_scan_list_t) +
                             selected_count * sizeof(wifi_manager_scan_item_t);
    wifi_manager_scan_list_t *list = heap_caps_malloc(
        allocation_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (list == NULL) {
        publish_status(WIFI_MANAGER_FAILED, NULL);
        return;
    }
    list->count = selected_count;
    memcpy(list->items, selected, selected_count * sizeof(selected[0]));
    log_memory("scan-records-released");
    if (s_scan_callback != NULL) {
        s_scan_callback(list, s_status_context);
    } else {
        heap_caps_free(list);
    }
}

static esp_err_t start_scan(void)
{
    stop_provisioning_server();
    stop_wifi();
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result == ESP_OK) result = esp_wifi_start();
    if (result == ESP_OK) {
        s_wifi_started = true;
        s_radio_mode = RADIO_SCANNING;
        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };
        log_memory("before-scan");
        result = esp_wifi_scan_start(&scan_config, false);
    }
    if (result != ESP_OK) {
        stop_wifi();
        return result;
    }
    publish_status(WIFI_MANAGER_SCANNING, NULL);
    return ESP_OK;
}

static void wifi_worker(void *context)
{
    (void)context;
    wifi_command_t command;
    while (xQueueReceive(s_command_queue, &command, portMAX_DELAY) == pdTRUE) {
        switch (command.type) {
            case COMMAND_START:
                if (s_saved_credentials.valid) {
                    s_active_credentials = s_saved_credentials;
                    if (start_sta(&s_active_credentials) != ESP_OK) schedule_retry();
                } else {
                    publish_status(WIFI_MANAGER_UNCONFIGURED, NULL);
                }
                break;
            case COMMAND_START_SCAN:
                s_candidate_pending = false;
                if (start_scan() != ESP_OK) publish_status(WIFI_MANAGER_FAILED, NULL);
                break;
            case COMMAND_SCAN_DONE:
                if (s_radio_mode == RADIO_SCANNING) publish_scan_results();
                break;
            case COMMAND_START_PROVISIONING:
                s_candidate_pending = false;
                stop_provisioning_server();
                if (start_provisioning_ap() != ESP_OK) {
                    publish_status(WIFI_MANAGER_FAILED, NULL);
                }
                break;
            case COMMAND_STOP_PROVISIONING:
                if (s_radio_mode == RADIO_SCANNING) esp_wifi_scan_stop();
                if (s_radio_mode != RADIO_PROVISIONING_AP &&
                    s_radio_mode != RADIO_SCANNING) break;
                stop_provisioning_server();
                if (s_saved_credentials.valid) {
                    s_active_credentials = s_saved_credentials;
                    if (start_sta(&s_active_credentials) != ESP_OK) schedule_retry();
                } else {
                    stop_wifi();
                    publish_status(WIFI_MANAGER_UNCONFIGURED, NULL);
                }
                break;
            case COMMAND_CONNECT_CANDIDATE:
                stop_provisioning_server();
                memset(&s_active_credentials, 0, sizeof(s_active_credentials));
                memcpy(s_active_credentials.ssid, command.ssid, command.ssid_len);
                s_active_credentials.ssid_len = command.ssid_len;
                strlcpy(s_active_credentials.password, command.password,
                        sizeof(s_active_credentials.password));
                s_active_credentials.valid = true;
                s_candidate_pending = true;
                s_candidate_attempts = 0;
                s_retry_attempts = 0;
                if (start_sta(&s_active_credentials) != ESP_OK) handle_disconnected();
                break;
            case COMMAND_STA_DISCONNECTED:
                handle_disconnected();
                break;
            case COMMAND_GOT_IP:
                handle_got_ip();
                break;
            case COMMAND_RETRY_NOW:
                stop_reconnect_timer();
                if (s_radio_mode == RADIO_PROVISIONING_AP) {
                    stop_provisioning_server();
                    stop_wifi();
                    if (!s_saved_credentials.valid) {
                        publish_status(WIFI_MANAGER_UNCONFIGURED, NULL);
                        break;
                    }
                    s_active_credentials = s_saved_credentials;
                }
                if (s_active_credentials.valid && s_radio_mode == RADIO_STA) {
                    publish_status(WIFI_MANAGER_CONNECTING, &s_active_credentials);
                    esp_err_t result = esp_wifi_connect();
                    if (result != ESP_OK) schedule_retry();
                } else if (s_saved_credentials.valid) {
                    s_active_credentials = s_saved_credentials;
                    if (start_sta(&s_active_credentials) != ESP_OK) schedule_retry();
                } else {
                    publish_status(WIFI_MANAGER_UNCONFIGURED, NULL);
                }
                break;
            case COMMAND_RECONNECT:
                stop_reconnect_timer();
                if (s_active_credentials.valid && s_radio_mode == RADIO_STA) {
                    publish_status(WIFI_MANAGER_CONNECTING, &s_active_credentials);
                    esp_err_t result = esp_wifi_connect();
                    if (result != ESP_OK) schedule_retry();
                } else if (s_saved_credentials.valid) {
                    s_active_credentials = s_saved_credentials;
                    if (start_sta(&s_active_credentials) != ESP_OK) schedule_retry();
                } else {
                    publish_status(WIFI_MANAGER_UNCONFIGURED, NULL);
                }
                break;
            case COMMAND_FORGET_NETWORK: {
                stop_provisioning_server();
                stop_wifi();
                esp_err_t result = erase_credentials();
                memset(&s_saved_credentials, 0, sizeof(s_saved_credentials));
                memset(&s_active_credentials, 0, sizeof(s_active_credentials));
                s_candidate_pending = false;
                publish_status(result == ESP_OK ? WIFI_MANAGER_UNCONFIGURED
                                                : WIFI_MANAGER_FAILED,
                               NULL);
                break;
            }
        }
    }
    vTaskDeleteWithCaps(NULL);
}

static void wifi_event_handler(void *argument, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    wifi_command_t command = {0};
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        command.type = COMMAND_STA_DISCONNECTED;
        post_command(&command);
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        command.type = COMMAND_SCAN_DONE;
        post_command(&command);
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        command.type = COMMAND_GOT_IP;
        post_command(&command);
    }
}

esp_err_t wifi_manager_init(wifi_manager_status_cb_t status_callback,
                            wifi_manager_scan_cb_t scan_callback,
                            void *context)
{
    s_status_callback = status_callback;
    s_scan_callback = scan_callback;
    s_status_context = context;

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = nvs_flash_init();
    if (result != ESP_OK) return result;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) return ESP_ERR_NO_MEM;

    log_memory("before-wifi-init");
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) return result;
    /* Candidate credentials stay in RAM until a successful DHCP lease. */
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) return result;
    log_memory("after-wifi-init");

    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 wifi_event_handler, NULL,
                                                 &s_wifi_event_instance);
    if (result == ESP_OK) {
        result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                     wifi_event_handler, NULL,
                                                     &s_ip_event_instance);
    }
    if (result != ESP_OK) return result;

    s_command_queue = xQueueCreate(WIFI_COMMAND_QUEUE_LENGTH, sizeof(wifi_command_t));
    if (s_command_queue == NULL) return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t reconnect_timer_args = {
        .callback = timer_reconnect, .name = "wifi_retry"};
    const esp_timer_create_args_t provisioning_timer_args = {
        .callback = timer_stop_provisioning, .name = "wifi_setup"};
    result = esp_timer_create(&reconnect_timer_args, &s_reconnect_timer);
    if (result == ESP_OK) {
        result = esp_timer_create(&provisioning_timer_args, &s_provisioning_timer);
    }
    if (result != ESP_OK) return result;

    result = load_credentials(&s_saved_credentials);
    if (result != ESP_OK) return result;
    generate_setup_credentials();

    if (xTaskCreateWithCaps(wifi_worker, "wifi_manager", WIFI_WORKER_STACK_BYTES,
                            NULL, 6, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    const wifi_command_t start = {.type = COMMAND_START};
    return post_command(&start) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_manager_start_scan(void)
{
    const wifi_command_t command = {.type = COMMAND_START_SCAN};
    return post_command(&command) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_manager_connect(const uint8_t *ssid, size_t ssid_len,
                               const char *password)
{
    if (ssid == NULL || ssid_len == 0 || ssid_len > 32 || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t password_len = strnlen(password, 65);
    if (password_len > 63 || (password_len > 0 && password_len < 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_command_t command = {.type = COMMAND_CONNECT_CANDIDATE};
    memcpy(command.ssid, ssid, ssid_len);
    command.ssid_len = ssid_len;
    memcpy(command.password, password, password_len);
    command.password[password_len] = '\0';
    return post_command(&command) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_manager_forget_network(void)
{
    const wifi_command_t command = {.type = COMMAND_FORGET_NETWORK};
    return post_command(&command) ? ESP_OK : ESP_FAIL;
}

void wifi_manager_free_scan_list(wifi_manager_scan_list_t *results)
{
    heap_caps_free(results);
}

esp_err_t wifi_manager_start_provisioning(void)
{
    generate_setup_credentials();
    const wifi_command_t command = {.type = COMMAND_START_PROVISIONING};
    return post_command(&command) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_manager_stop_provisioning(void)
{
    const wifi_command_t command = {.type = COMMAND_STOP_PROVISIONING};
    return post_command(&command) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_manager_retry(void)
{
    const wifi_command_t command = {.type = COMMAND_RETRY_NOW};
    return post_command(&command) ? ESP_OK : ESP_FAIL;
}

void wifi_manager_get_setup_credentials(char *ssid, size_t ssid_size,
                                        char *password, size_t password_size)
{
    if (ssid != NULL && ssid_size > 0) strlcpy(ssid, s_setup_ssid, ssid_size);
    if (password != NULL && password_size > 0) {
        strlcpy(password, s_setup_password, password_size);
    }
}
