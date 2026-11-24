#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    WIFI_MANAGER_UNCONFIGURED,
    WIFI_MANAGER_SCANNING,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_ONLINE,
    WIFI_MANAGER_RETRY_WAIT,
    WIFI_MANAGER_PROVISIONING,
    WIFI_MANAGER_FAILED,
} wifi_manager_state_t;

typedef struct {
    wifi_manager_state_t state;
    char ssid[33];
} wifi_manager_status_t;

typedef enum {
    WIFI_MANAGER_AUTH_OPEN,
    WIFI_MANAGER_AUTH_PERSONAL,
    WIFI_MANAGER_AUTH_UNSUPPORTED,
} wifi_manager_auth_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_len;
    char display_ssid[49];
    int8_t rssi;
    wifi_manager_auth_t auth;
} wifi_manager_scan_item_t;

typedef struct {
    size_t count;
    wifi_manager_scan_item_t items[];
} wifi_manager_scan_list_t;

typedef void (*wifi_manager_status_cb_t)(const wifi_manager_status_t *status,
                                         void *context);
typedef void (*wifi_manager_scan_cb_t)(wifi_manager_scan_list_t *results,
                                       void *context);

esp_err_t wifi_manager_init(wifi_manager_status_cb_t status_callback,
                            wifi_manager_scan_cb_t scan_callback,
                            void *context);
esp_err_t wifi_manager_start_scan(void);
esp_err_t wifi_manager_connect(const uint8_t *ssid, size_t ssid_len,
                               const char *password);
esp_err_t wifi_manager_forget_network(void);
void wifi_manager_free_scan_list(wifi_manager_scan_list_t *results);
esp_err_t wifi_manager_start_provisioning(void);
esp_err_t wifi_manager_stop_provisioning(void);
esp_err_t wifi_manager_retry(void);
void wifi_manager_get_setup_credentials(char *ssid, size_t ssid_size,
                                        char *password, size_t password_size);
