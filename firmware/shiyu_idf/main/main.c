#include <assert.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_lv_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "rgb_lcd_port.h"
#include "photo_portal.h"
#include "photo_storage.h"
#include "ui_controller.h"

static const char *TAG = "shiyu_idf";

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "Memory %s: internal=%u largest=%u psram=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void start_upload_task(void *context)
{
    (void)context;
    esp_err_t result = photo_portal_start();
    if (result != ESP_OK && esp_lv_adapter_lock(-1) == ESP_OK) {
        shiyu_ui_set_upload_state("传图暂不可用");
        shiyu_ui_set_portal_available(false);
        esp_lv_adapter_unlock();
    }
    vTaskDeleteWithCaps(NULL);
}

static void stop_upload_task(void *context)
{
    (void)context;
    photo_portal_stop();
    vTaskDeleteWithCaps(NULL);
}

static void start_upload(void)
{
    if (xTaskCreateWithCaps(start_upload_task, "start_upload", 4096, NULL, 5, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        shiyu_ui_set_upload_state("无法启动 Wi-Fi 任务");
    }
}

static void stop_upload(void)
{
    if (xTaskCreateWithCaps(stop_upload_task, "stop_upload", 4096, NULL, 5, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create Wi-Fi stop task");
    }
}

static void on_portal_state(photo_portal_state_t state, void *context)
{
    (void)context;
    if (esp_lv_adapter_lock(-1) != ESP_OK) return;
    switch (state) {
        case PHOTO_PORTAL_WAITING:
            shiyu_ui_set_upload_state("等待手机连接");
            break;
        case PHOTO_PORTAL_RECEIVING:
            shiyu_ui_set_upload_state("正在接收照片");
            break;
        case PHOTO_PORTAL_SAVED:
            shiyu_ui_set_upload_state("照片已保存");
            shiyu_ui_set_ambient_photo(PHOTO_STORAGE_LVGL_PATH);
            break;
        case PHOTO_PORTAL_ERROR:
            shiyu_ui_set_upload_state("上传失败，请重试");
            break;
        case PHOTO_PORTAL_STOPPED:
            break;
    }
    esp_lv_adapter_unlock();
}

void app_main(void)
{
    ESP_ERROR_CHECK(photo_storage_init() ? ESP_OK : ESP_FAIL);
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;
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

    /* Reserve Wi-Fi's internal-only buffers before creating optional UI pages. */
    log_memory("before-portal-init");
    esp_err_t portal_result = photo_portal_init(on_portal_state, NULL);
    if (portal_result != ESP_OK) {
        ESP_LOGE(TAG, "Photo portal unavailable: %s", esp_err_to_name(portal_result));
    }
    log_memory("after-portal-init");

    ESP_LOGI(TAG, "Create Shi Yu minimum state prototype");
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        esp_lv_decoder_handle_t decoder = NULL;
        ESP_ERROR_CHECK(esp_lv_decoder_init(&decoder));
        shiyu_ui_create(start_upload, stop_upload);
        shiyu_ui_set_portal_available(portal_result == ESP_OK);
        if (photo_storage_has_photo()) {
            shiyu_ui_set_ambient_photo(PHOTO_STORAGE_LVGL_PATH);
        }
        esp_lv_adapter_unlock();
    }
    log_memory("after-resident-ui");
}
