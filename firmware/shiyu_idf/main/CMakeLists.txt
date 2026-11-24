idf_component_register(
    SRCS "main.c" "ui_controller.c" "photo_storage.c" "wifi_manager.c" "cloud_sync.c" "carousel_settings.c"
         "fonts/shiyu_font_28.c" "fonts/shiyu_font_cjk_24.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_http_client
        esp-tls
        mbedtls
        json
        esp_new_jpeg
        esp_event
        esp_http_server
        esp_timer
        esp_lvgl_adapter
        esp_lv_decoder
        esp_netif
        esp_wifi
        fatfs
        nvs_flash
        rgb_lcd_port
        wear_levelling
    WHOLE_ARCHIVE
    )

# The project disables LV_USE_FONT_COMPRESSED. Keep the shared body font in
# LVGL's directly-renderable 2 bpp bitmap format so a regenerated font cannot
# silently compile and render blank on the device.
file(READ "${CMAKE_CURRENT_LIST_DIR}/fonts/shiyu_font_cjk_24.c" SHIYU_BODY_FONT_SOURCE)
if(NOT SHIYU_BODY_FONT_SOURCE MATCHES "\\.bpp = 2," OR
   NOT SHIYU_BODY_FONT_SOURCE MATCHES "\\.bitmap_format = 0,")
    message(FATAL_ERROR
        "shiyu_font_cjk_24 must be an uncompressed LVGL 2 bpp font (bitmap_format=0)")
endif()
unset(SHIYU_BODY_FONT_SOURCE)
    
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format) 
