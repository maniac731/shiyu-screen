#include "photo_storage.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "photo_storage";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

bool photo_storage_init(void)
{
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };
    esp_err_t result = esp_vfs_fat_spiflash_mount_rw_wl(
        PHOTO_STORAGE_MOUNT_POINT, "storage", &mount_config, &s_wl_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FAT mount failed: %s", esp_err_to_name(result));
        return false;
    }
    ESP_LOGI(TAG, "FAT storage mounted");
    return true;
}

bool photo_storage_has_photo(void)
{
    struct stat info;
    return stat(PHOTO_STORAGE_CURRENT_PATH, &info) == 0 && info.st_size > 0;
}

FILE *photo_storage_open_upload(void)
{
    unlink(PHOTO_STORAGE_TEMP_PATH);
    return fopen(PHOTO_STORAGE_TEMP_PATH, "wb");
}

bool photo_storage_accept_upload(size_t received_bytes)
{
    if (received_bytes == 0 || received_bytes > PHOTO_STORAGE_MAX_BYTES) return false;

    FILE *file = fopen(PHOTO_STORAGE_TEMP_PATH, "rb");
    if (file == NULL) return false;
    unsigned char signature[2] = {0};
    const bool is_jpeg = fread(signature, 1, sizeof(signature), file) == sizeof(signature) &&
                         signature[0] == 0xFF && signature[1] == 0xD8;
    fclose(file);
    if (!is_jpeg) return false;

    unlink(PHOTO_STORAGE_CURRENT_PATH);
    if (rename(PHOTO_STORAGE_TEMP_PATH, PHOTO_STORAGE_CURRENT_PATH) != 0) return false;
    ESP_LOGI(TAG, "Accepted JPEG: %u bytes", (unsigned)received_bytes);
    return true;
}

void photo_storage_discard_upload(void)
{
    unlink(PHOTO_STORAGE_TEMP_PATH);
}
