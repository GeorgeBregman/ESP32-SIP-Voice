# Main component CMakeLists.txt
idf_component_register(SRCS
                       "main.c"
                       "wifi_manager.c"
                       "sip_client.c"
                       "rtp_handler.c"
                       "audio_pipeline.c"
                       "g711_codec.c"
                       "codec_driver.c"
                       INCLUDE_DIRS "."
                       REQUIRES lwip # Add other required components like driver, freertos, esp_wifi, etc.
                      )

# If using ESP-ADF components for codec drivers, add them here:
# REQUIRES esxxx_codec esp_codec_dev ...
