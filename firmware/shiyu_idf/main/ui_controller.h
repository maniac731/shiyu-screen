#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_manager.h"

typedef void (*shiyu_ui_action_cb_t)(void);
typedef void (*shiyu_ui_connect_cb_t)(const uint8_t *ssid, size_t ssid_len,
                                      const char *password);

void shiyu_ui_create(shiyu_ui_action_cb_t start_network_scan,
                     shiyu_ui_action_cb_t start_network_setup,
                     shiyu_ui_action_cb_t stop_network_setup,
                     shiyu_ui_action_cb_t retry_network,
                     shiyu_ui_action_cb_t forget_network,
                     shiyu_ui_connect_cb_t connect_network);
void shiyu_ui_show_scan_results(const wifi_manager_scan_list_t *results);
void shiyu_ui_set_network_state(const char *status, const char *ssid);
void shiyu_ui_set_network_setup_info(const char *ssid, const char *password);
void shiyu_ui_set_ambient_photo(const char *lvgl_path);
void shiyu_ui_set_cloud_state(const char *text, const char *binding_url);
