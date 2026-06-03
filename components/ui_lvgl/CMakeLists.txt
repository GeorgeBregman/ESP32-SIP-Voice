idf_component_register(SRCS "ui_lvgl.c"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl hal esp_timer esp_lcd config_store)
