idf_component_register(
    SRCS "main.c" "ui_controller.c" "photo_storage.c" "photo_portal.c"
         "fonts/shiyu_font_24.c" "fonts/shiyu_font_28.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_event
        esp_http_server
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
    
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format) 
