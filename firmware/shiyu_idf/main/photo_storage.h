#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define PHOTO_STORAGE_MOUNT_POINT "/storage"
#define PHOTO_STORAGE_TEMP_PATH "/storage/cloud.tmp"
#define PHOTO_STORAGE_MAX_BYTES (1024U * 1024U)

bool photo_storage_init(void);
bool photo_storage_has_photo(void);
FILE *photo_storage_open_upload(void);
void photo_storage_discard_upload(void);
const char *photo_storage_active_lvgl_path(void);
const char *photo_storage_cloud_id(void);
bool photo_storage_commit_cloud(const char *photo_id);

bool photo_storage_snapshot(char *out, size_t size, bool advance);
unsigned photo_storage_count(void);
bool photo_storage_contains(const char *id);
bool photo_storage_prepare_download(size_t bytes);
bool photo_storage_reconcile(const char ids[][37], unsigned count);

/* Called with LVGL drawing quiescent; dirty reports possible output mutation. */
bool photo_storage_decode_frame(const char *path, void *pixels, size_t capacity, bool *dirty);
