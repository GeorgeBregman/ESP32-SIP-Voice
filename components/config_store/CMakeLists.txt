# Shared low-level configuration: app_config.h (compile-time defaults) and the
# NVS-backed runtime settings (config_manager). Lives in its own component so
# that hal / ui_lvgl / audio_pipeline / main can all depend on it without any
# circular dependency on `main`.
idf_component_register(SRCS "config_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash)
