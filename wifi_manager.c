#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h" // For basic DNS server
#include <string.h>

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;
static EventBits_t s_connected_bit;
static EventBits_t s_ip_acquired_bit;
static int s_retry_num = 0;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static app_settings_t *g_settings = NULL;
static bool s_is_ap_mode = false;

// DNS Server Task for Captive Portal
static void dns_server_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);

    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    char rx_buffer[128];
    char tx_buffer[128];
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &addr_len);
        if (len > 0) {
            // Very dumb DNS responder: answers any A record request with our AP IP (192.168.4.1)
            memcpy(tx_buffer, rx_buffer, len);
            tx_buffer[2] |= 0x80; // Set response flag
            tx_buffer[3] |= 0x80; // Recursion available
            tx_buffer[7] = 1; // 1 answer
            
            // Build answer
            int ptr = len;
            tx_buffer[ptr++] = 0xc0;
            tx_buffer[ptr++] = 0x0c; // Pointer to name
            tx_buffer[ptr++] = 0x00;
            tx_buffer[ptr++] = 0x01; // Type A
            tx_buffer[ptr++] = 0x00;
            tx_buffer[ptr++] = 0x01; // Class IN
            tx_buffer[ptr++] = 0x00;
            tx_buffer[ptr++] = 0x00;
            tx_buffer[ptr++] = 0x00;
            tx_buffer[ptr++] = 0x3c; // TTL 60
            tx_buffer[ptr++] = 0x00;
            tx_buffer[ptr++] = 0x04; // Data length 4
            // 192.168.4.1
            tx_buffer[ptr++] = 192;
            tx_buffer[ptr++] = 168;
            tx_buffer[ptr++] = 4;
            tx_buffer[ptr++] = 1;

            sendto(sock, tx_buffer, ptr, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
    }
}

static void start_ap_mode(void) {
    s_is_ap_mode = true;
    ESP_LOGI(TAG, "Starting AP Mode: ESP-SIP-Setup");
    esp_wifi_stop();
    
    ap_netif = esp_netif_create_default_wifi_ap();
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP-SIP-Setup",
            .ssid_len = strlen("ESP-SIP-Setup"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    // Start DNS server for Captive Portal
    xTaskCreate(dns_server_task, "dns_task", 2048, NULL, 5, NULL);
    
    // Notify application that AP mode is ready (IP is 192.168.4.1)
    xEventGroupSetBits(s_wifi_event_group, s_connected_bit | s_ip_acquired_bit);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (strlen(g_settings->wifi_ssid) > 0) {
            esp_wifi_connect();
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Connecting to %s...", g_settings->wifi_ssid);
        } else {
            ESP_LOGW(TAG, "No Wi-Fi credentials found, starting AP immediately.");
            start_ap_mode();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_is_ap_mode) return;
        
        if (s_retry_num < 3) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to the AP (%d/3)", s_retry_num);
        } else {
            ESP_LOGE(TAG, "Failed to connect to the AP. Falling back to AP Mode (Captive Portal).");
            start_ap_mode();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, s_connected_bit | s_ip_acquired_bit);
    }
}

void wifi_init_sta(EventGroupHandle_t wifi_event_group, const EventBits_t connected_bit, const EventBits_t ip_acquired_bit, app_settings_t *settings) {
    g_settings = settings;
    s_wifi_event_group = wifi_event_group;
    s_connected_bit = connected_bit;
    s_ip_acquired_bit = ip_acquired_bit;

    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };
    strncpy((char *)wifi_config.sta.ssid, settings->wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, settings->wifi_password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
}

esp_err_t get_my_ip(esp_ip4_addr_t *ip_addr) {
    if (s_is_ap_mode && ap_netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(ap_netif, &ip_info);
        if (ip_addr) *ip_addr = ip_info.ip;
        return ESP_OK;
    } else if (sta_netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(sta_netif, &ip_info);
        if (ip_addr) *ip_addr = ip_info.ip;
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool wifi_is_ap_mode(void) {
    return s_is_ap_mode;
}
