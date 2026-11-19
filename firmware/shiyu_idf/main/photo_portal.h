#pragma once

#include "esp_err.h"

typedef enum {
    PHOTO_PORTAL_WAITING,
    PHOTO_PORTAL_RECEIVING,
    PHOTO_PORTAL_SAVED,
    PHOTO_PORTAL_ERROR,
    PHOTO_PORTAL_STOPPED,
} photo_portal_state_t;

typedef void (*photo_portal_state_cb_t)(photo_portal_state_t state, void *context);

esp_err_t photo_portal_init(photo_portal_state_cb_t callback, void *context);
esp_err_t photo_portal_start(void);
void photo_portal_stop(void);
