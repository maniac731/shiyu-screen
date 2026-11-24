#include <assert.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_lv_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "rgb_lcd_port.h"
#include "photo_storage.h"
#include "inventory_model.h"
#include "ui_controller.h"
#include "wifi_manager.h"
#include "cloud_sync.h"
#include "nvs_flash.h"

static const char *TAG = "shiyu_idf";

typedef enum {
    NETWORK_UI_STATUS,
    NETWORK_UI_SCAN_RESULTS,
} network_ui_event_type_t;

typedef struct {
    network_ui_event_type_t type;
    union {
        wifi_manager_status_t status;
        wifi_manager_scan_list_t *scan_results;
    } data;
} network_ui_event_t;

static QueueHandle_t s_network_ui_queue;

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "Memory %s: internal=%u largest=%u psram=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static const char *network_state_text(wifi_manager_state_t state)
{
    switch (state) {
        case WIFI_MANAGER_UNCONFIGURED:
            return "尚未配置";
        case WIFI_MANAGER_SCANNING:
            return "正在扫描";
        case WIFI_MANAGER_CONNECTING:
            return "正在连接";
        case WIFI_MANAGER_ONLINE:
            return "已连接";
        case WIFI_MANAGER_RETRY_WAIT:
            return "离线，正在重试";
        case WIFI_MANAGER_PROVISIONING:
            return "等待手机配置";
        case WIFI_MANAGER_FAILED:
            return "连接失败";
        default:
            return "网络状态未知";
    }
}

static void on_network_status(const wifi_manager_status_t *status, void *context)
{
    (void)context;
    if (status == NULL || s_network_ui_queue == NULL) return;
    network_ui_event_t event = {
        .type = NETWORK_UI_STATUS,
        .data.status = *status,
    };
    if (xQueueSend(s_network_ui_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Network UI queue full; status update skipped");
    }
}

static void on_network_scan_results(wifi_manager_scan_list_t *results, void *context)
{
    (void)context;
    if (results == NULL) return;
    if (s_network_ui_queue == NULL) {
        wifi_manager_free_scan_list(results);
        return;
    }
    network_ui_event_t event = {
        .type = NETWORK_UI_SCAN_RESULTS,
        .data.scan_results = results,
    };
    if (xQueueSend(s_network_ui_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Network UI queue full; scan results released");
        wifi_manager_free_scan_list(results);
    }
}

static void network_ui_task(void *context)
{
    (void)context;
    network_ui_event_t event;
    while (xQueueReceive(s_network_ui_queue, &event, portMAX_DELAY) == pdTRUE) {
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            if (event.type == NETWORK_UI_STATUS) {
                const wifi_manager_status_t *status = &event.data.status;
                shiyu_ui_set_network_state(network_state_text(status->state),
                                           status->ssid);
            } else if (event.type == NETWORK_UI_SCAN_RESULTS) {
                shiyu_ui_show_scan_results(event.data.scan_results);
            }
            esp_lv_adapter_unlock();
        }
        if (event.type == NETWORK_UI_SCAN_RESULTS) {
            wifi_manager_free_scan_list(event.data.scan_results);
        }
    }
    vTaskDeleteWithCaps(NULL);
}

static void start_network_setup(void)
{
    char ssid[33] = {0};
    char password[16] = {0};
    if (wifi_manager_start_provisioning() != ESP_OK) {
        shiyu_ui_set_network_state("无法启动配网", NULL);
        return;
    }
    wifi_manager_get_setup_credentials(ssid, sizeof(ssid), password, sizeof(password));
    shiyu_ui_set_network_setup_info(ssid, password);
}

static void start_network_scan(void)
{
    if (wifi_manager_start_scan() != ESP_OK) {
        shiyu_ui_set_network_state("无法开始扫描", NULL);
    }
}

static void stop_network_setup(void)
{
    if (wifi_manager_stop_provisioning() != ESP_OK) {
        ESP_LOGE(TAG, "Unable to stop network setup");
    }
}

static void retry_network(void)
{
    if (wifi_manager_retry() != ESP_OK) {
        shiyu_ui_set_network_state("无法重新连接", NULL);
    }
}

static void forget_network(void)
{
    if (wifi_manager_forget_network() != ESP_OK) {
        shiyu_ui_set_network_state("无法忘记网络", NULL);
    }
}

static void connect_network(const uint8_t *ssid, size_t ssid_len,
                            const char *password)
{
    if (wifi_manager_connect(ssid, ssid_len, password) != ESP_OK) {
        shiyu_ui_set_network_state("无法连接网络", NULL);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(photo_storage_init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(inventory_model_init() ? ESP_OK : ESP_FAIL);
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_PARTIAL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        tear_mode, rotation, &panel_handle, &touch_handle));
    wavesahre_rgb_lcd_bl_on();

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation);
    display_config.profile.use_psram = true;
    display_config.tear_avoid_mode = tear_mode;
    lv_display_t *display = esp_lv_adapter_register_display(&display_config);
    assert(display != NULL);

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());
    log_memory("after-lvgl");

    s_network_ui_queue = xQueueCreate(8, sizeof(network_ui_event_t));
    if (s_network_ui_queue == NULL) {
        ESP_LOGE(TAG, "Unable to create network UI queue");
    }

    /* Reserve Wi-Fi's internal-only buffers before creating optional UI pages. */
    log_memory("before-wifi-manager-init");
    esp_err_t wifi_result = s_network_ui_queue == NULL
                                ? ESP_ERR_NO_MEM
                                : wifi_manager_init(on_network_status,
                                                    on_network_scan_results,
                                                    NULL);
    if (wifi_result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager unavailable: %s", esp_err_to_name(wifi_result));
    }
    log_memory("after-wifi-manager-init");

    ESP_LOGI(TAG, "Create Shi Yu minimum state prototype");
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        esp_lv_decoder_handle_t decoder = NULL;
        ESP_ERROR_CHECK(esp_lv_decoder_init(&decoder));
        shiyu_ui_create(start_network_scan, start_network_setup,
                        stop_network_setup, retry_network, forget_network,
                        connect_network);
        if (wifi_result != ESP_OK) shiyu_ui_set_network_state("网络暂不可用", NULL);
        if (photo_storage_has_photo()) {
            shiyu_ui_set_ambient_photo(photo_storage_active_lvgl_path());
        }
        esp_lv_adapter_unlock();
    }
    if (s_network_ui_queue != NULL &&
        xTaskCreateWithCaps(network_ui_task, "network_ui", 4096, NULL, 5, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create network UI task");
    }
    log_memory("after-resident-ui");
    esp_err_t cloud_result = cloud_sync_init();
    if (cloud_result != ESP_OK) ESP_LOGE(TAG, "Cloud worker unavailable: %s", esp_err_to_name(cloud_result));
}
