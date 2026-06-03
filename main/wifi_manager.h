#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif_types.h"
#include "config_manager.h"
#include <stdbool.h>

void wifi_init_sta(EventGroupHandle_t wifi_event_group, const EventBits_t connected_bit, const EventBits_t ip_acquired_bit, app_settings_t *settings);
esp_err_t get_my_ip(esp_ip4_addr_t *ip_addr);
bool wifi_is_ap_mode(void);

#endif // WIFI_MANAGER_H
