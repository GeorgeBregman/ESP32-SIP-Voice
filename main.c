#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "sip_client.h"
#include "audio_pipeline.h"
#include "app_config.h" // Include configurations
#include "ui_controller.h" // Include UI Controller
#include "config_manager.h"
#include "display.h"
#include "phonebook.h"
#include "keypad.h"
#include "display_tft.h"
#include "touch_driver.h"
#include "ui_lvgl.h"

static const char *TAG = "MAIN";

// Event group to signal application state
EventGroupHandle_t app_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define SIP_REGISTERED_BIT  BIT1
#define IP_ACQUIRED_BIT     BIT2 // Added for clarity

// Shared handles (if needed across modules, though better passed as params)
sip_client_handle_t g_sip_client = NULL;
audio_pipeline_handle_t g_audio_pipeline = NULL;
app_settings_t g_app_settings;

// --- Application State Machine (Simplified Example) ---
typedef enum {
    APP_STATE_INIT,
    APP_STATE_WIFI_CONNECTING,
    APP_STATE_SIP_REGISTERING,
    APP_STATE_IDLE, // Registered, ready for calls
    APP_STATE_IN_CALL,
    APP_STATE_ERROR
} app_state_t;

volatile app_state_t current_app_state = APP_STATE_INIT;

void app_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Application control task started.");
    current_app_state = APP_STATE_WIFI_CONNECTING;

    while(1) {
        EventBits_t bits = xEventGroupWaitBits(app_event_group,
                                             WIFI_CONNECTED_BIT | SIP_REGISTERED_BIT,
                                             pdFALSE, // Don't clear on exit
                                             pdFALSE, // Wait for ANY bit (might change based on logic)
                                             portMAX_DELAY);

        if ((bits & WIFI_CONNECTED_BIT) && current_app_state < APP_STATE_SIP_REGISTERING) {
             ESP_LOGI(TAG, "Wi-Fi Connected. Starting SIP Registration.");
             current_app_state = APP_STATE_SIP_REGISTERING;
             // Signal SIP task to start registration (if not already implicitly started)
             if (g_sip_client) {
                 sip_client_start_registration(g_sip_client);
             }
        }

        if ((bits & SIP_REGISTERED_BIT) && current_app_state < APP_STATE_IDLE) {
            ESP_LOGI(TAG, "SIP Registered. Application Idle.");
            current_app_state = APP_STATE_IDLE;
            // Maybe trigger an LED or other indicator
        }

        // Add logic here to react to call state changes signaled FROM sip_client
        // e.g., if sip_client signals incoming call, update state, maybe ring a buzzer
        // if sip_client signals call active, update state
        // if sip_client signals call ended, return to APP_STATE_IDLE

        vTaskDelay(pdMS_TO_TICKS(500)); // Check state periodically
    }
}


void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 SIP Client Application");

    // Initialize NVS Flash & Load Config
    config_manager_init();
    config_manager_load(&g_app_settings);
    
    // Initialize Phonebook Storage
    phonebook_init();
    
    // Initialize HAL Components
    keypad_init();
    display_tft_init();
    touch_driver_init();
    
    // Initialize Graphic UI
    ui_lvgl_init();
    
    // Initialize Display
    display_init();
    display_update_status("Starting...", "Init", "");

    // Create Event Group
    app_event_group = xEventGroupCreate();
    if (app_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return; // Or handle error appropriately
    }

    // Initialize TCP/IP stack and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize Wi-Fi (will block or start AP if missing config)
    wifi_init_sta(app_event_group, WIFI_CONNECTED_BIT, IP_ACQUIRED_BIT, &g_app_settings); 

    if (wifi_is_ap_mode()) {
        ESP_LOGI(TAG, "Running in AP Mode (Captive Portal). SIP disabled.");
        display_update_status("192.168.4.1", "AP Setup Mode", "ESP-SIP-Setup");
        ui_controller_init(NULL, &g_app_settings);
        return; // Stop initialization here
    }

    esp_ip4_addr_t my_ip;
    get_my_ip(&my_ip);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&my_ip));
    display_update_status(ip_str, "Connecting SIP...", "");

    // Initialize Audio Pipeline (I2S, Codec)
    // This needs the actual codec driver implementation
    g_audio_pipeline = audio_pipeline_init();
    if (!g_audio_pipeline) {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline");
        // Handle error - maybe cannot proceed
    }

    // Initialize SIP Client
    // Pass necessary handles/configs
    g_sip_client = sip_client_init(app_event_group, SIP_REGISTERED_BIT, g_audio_pipeline, &g_app_settings);
     if (!g_sip_client) {
        ESP_LOGE(TAG, "Failed to initialize SIP client");
        // Handle error
    } else {
        // Link audio pipeline back to SIP client if needed for RTP data
         if (g_audio_pipeline) {
            audio_pipeline_set_sip_handle(g_audio_pipeline, g_sip_client);
         }
    }

    // Initialize UI Controller
    if (g_sip_client) {
        ui_controller_init(g_sip_client, &g_app_settings);
    }

    if (g_audio_pipeline) {
        audio_pipeline_set_wake_word_cb(g_audio_pipeline, ui_controller_wake_word_trigger);
    }

    // Create application control task (optional, but good for managing overall state)
    xTaskCreate(app_control_task, "app_ctrl", 4096, NULL, 6, NULL);


    ESP_LOGI(TAG, "Initialization Complete. Waiting for Wi-Fi connection...");

    // Tasks for WiFi events, SIP, Audio are created within their respective init functions usually.
    // The system now runs on FreeRTOS tasks. app_main finishes here.
}
