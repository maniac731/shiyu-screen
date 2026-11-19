#pragma once

#include <stdbool.h>

typedef void (*shiyu_ui_action_cb_t)(void);

void shiyu_ui_create(shiyu_ui_action_cb_t start_upload, shiyu_ui_action_cb_t stop_upload);
void shiyu_ui_set_portal_available(bool available);
void shiyu_ui_set_upload_state(const char *status);
void shiyu_ui_set_ambient_photo(const char *lvgl_path);
