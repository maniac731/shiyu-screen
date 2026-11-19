#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define PHOTO_STORAGE_MOUNT_POINT "/storage"
#define PHOTO_STORAGE_TEMP_PATH "/storage/upload.tmp"
#define PHOTO_STORAGE_CURRENT_PATH "/storage/current.jpg"
#define PHOTO_STORAGE_LVGL_PATH "S:/current.jpg"
#define PHOTO_STORAGE_MAX_BYTES (1024U * 1024U)

bool photo_storage_init(void);
bool photo_storage_has_photo(void);
FILE *photo_storage_open_upload(void);
bool photo_storage_accept_upload(size_t received_bytes);
void photo_storage_discard_upload(void);
