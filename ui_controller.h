#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include "esp_err.h"
#include "sip_client.h"

esp_err_t ui_controller_init(sip_client_handle_t sip_client);

#endif // UI_CONTROLLER_H
