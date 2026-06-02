#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "esp_err.h"
#include "sip_client.h"
#include "config_manager.h"

esp_err_t ui_controller_init(sip_client_handle_t sip_client, app_settings_t *settings);

#endif // UI_CONTROLLER_H
