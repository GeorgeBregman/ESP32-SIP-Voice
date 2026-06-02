#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "esp_err.h"

typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char sip_server[32];
    char sip_user[32];
    char sip_password[64];
} app_settings_t;

esp_err_t config_manager_init(void);
esp_err_t config_manager_load(app_settings_t *settings);
esp_err_t config_manager_save(const app_settings_t *settings);
void config_manager_reset(void); // Clear NVS

#endif // CONFIG_MANAGER_H
