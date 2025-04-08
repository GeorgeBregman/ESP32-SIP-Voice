// wifi_manager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif_types.h" // For esp_ip4_addr_t

void wifi_init_sta(EventGroupHandle_t wifi_event_group, const EventBits_t connected_bit, const EventBits_t ip_acquired_bit);
esp_err_t get_my_ip(esp_ip4_addr_t *ip_addr); // Utility function

#endif // WIFI_MANAGER_H

// wifi_manager.c
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "app_config.h" // For credentials

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;
static EventBits_t s_connected_bit;
static EventBits_t s_ip_acquired_bit;
static int s_retry_num = 0;
static esp_netif_t *sta_netif = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to the AP (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupClearBits(s_wifi_event_group, s_connected_bit | s_ip_acquired_bit); // Clear both
            ESP_LOGE(TAG, "Failed to connect to the AP after max retries.");
            // Consider signaling an error state to the application
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, s_connected_bit | s_ip_acquired_bit); // Set both
    } else {
         ESP_LOGD(TAG, "Unhandled event: base=%s, id=%ld", event_base, event_id);
    }
}

void wifi_init_sta(EventGroupHandle_t wifi_event_group, const EventBits_t connected_bit, const EventBits_t ip_acquired_bit) {
    s_wifi_event_group = wifi_event_group;
    s_connected_bit = connected_bit;
    s_ip_acquired_bit = ip_acquired_bit;

    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Adjust as needed
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

esp_err_t get_my_ip(esp_ip4_addr_t *ip_addr) {
    if (!sta_netif) {
        return ESP_FAIL;
    }
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(sta_netif, &ip_info);
    if (ret == ESP_OK && ip_addr) {
        *ip_addr = ip_info.ip;
    }
    return ret;
}
