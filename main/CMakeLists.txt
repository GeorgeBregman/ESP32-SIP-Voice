# Main application component.
#
# All of the firmware "app" translation units live here. Hardware drivers,
# the DSP codecs and the LVGL UI live in their own components under
# ../components and are pulled in through REQUIRES so their public headers
# are on the include path.
idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "sip_client.c"
        "rtp_handler.c"
        "audio_pipeline.c"
        "g711_codec.c"
        "codec_driver.c"
        "ui_controller.c"
        "display.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        # System components
        lwip
        mbedtls
        nvs_flash
        esp-tls
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp_timer
        driver
        json
        # Project components (../components/*)
        config_store
        audio_pipeline
        hal
        storage
        ui_lvgl
)
