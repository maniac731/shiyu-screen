#include "carousel_settings.h"
#include <stdatomic.h>
#include "nvs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

static atomic_uint s_seconds = 10, s_pending;
static atomic_int s_result;
static TaskHandle_t s_worker;

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        unsigned next = atomic_load(&s_pending);
        nvs_handle_t n;
        esp_err_t err = nvs_open("carousel", NVS_READWRITE, &n);
        if (err == ESP_OK) {
            err = nvs_set_u32(n, "seconds", next);
            if (err == ESP_OK) err = nvs_commit(n);
            nvs_close(n);
        }
        if (err == ESP_OK) atomic_store(&s_seconds, next);
        atomic_store(&s_result, err == ESP_OK ? 1 : -1);
    }
}

bool carousel_settings_init(void)
{
    nvs_handle_t n;
    uint32_t seconds = 10;
    if (nvs_open("carousel", NVS_READONLY, &n) == ESP_OK) {
        nvs_get_u32(n, "seconds", &seconds);
        nvs_close(n);
    }
    if (seconds < 5 || seconds > 300) seconds = 10;
    atomic_store(&s_seconds, seconds);
    return xTaskCreateWithCaps(worker, "carousel_save", 3072, NULL, 2, &s_worker,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

unsigned carousel_settings_seconds(void) { return atomic_load(&s_seconds); }

bool carousel_settings_save(unsigned seconds)
{
    if (seconds < 5 || seconds > 300 || !s_worker || atomic_load(&s_result) == 2)
        return false;
    atomic_store(&s_pending, seconds);
    atomic_store(&s_result, 2);
    xTaskNotifyGive(s_worker);
    return true;
}

int carousel_settings_result(void) { return atomic_load(&s_result); }
