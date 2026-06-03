idf_component_register(SRCS "keypad.c" 
                            "display_tft.c"
                            "touch_driver.c"
                       INCLUDE_DIRS "."
                    REQUIRES driver esp_lcd)
