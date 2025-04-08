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

static const char *TAG = "MAIN";

// Event group to signal application state
EventGroupHandle_t app_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define SIP_REGISTERED_BIT  BIT1
#define IP_ACQUIRED_BIT     BIT2 // Added for clarity

// Shared handles (if needed across modules, though better passed as params)
sip_client_handle_t g_sip_client = NULL;
audio_pipeline_handle_t g_audio_pipeline = NULL;

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

        // --- Example Call Initiation (triggered by button press perhaps) ---
        // bool initiate_call = check_call_button(); // Placeholder
        // if (initiate_call && current_app_state == APP_STATE_IDLE) {
        //     ESP_LOGI(TAG, "Initiating outbound call...");
        //     if (g_sip_client) {
        //          // Target URI needs to be obtained somehow (e.g., config, input)
        //         sip_client_initiate_call(g_sip_client, "sip:target_user@your_domain.com");
        //     }
        // }

        // Add logic here to react to call state changes signaled FROM sip_client
        // e.g., if sip_client signals incoming call, update state, maybe ring a buzzer
        // if sip_client signals call active, update state
        // if sip_client signals call ended, return to APP_STATE_IDLE

        vTaskDelay(pdMS_TO_TICKS(500)); // Check state periodically
    }
}


void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 SIP Client Application");

    // Initialize NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create Event Group
    app_event_group = xEventGroupCreate();
    if (app_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return; // Or handle error appropriately
    }

    // Initialize TCP/IP stack and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize Wi-Fi
    wifi_init_sta(app_event_group, WIFI_CONNECTED_BIT, IP_ACQUIRED_BIT); // Pass event group

    // Initialize Audio Pipeline (I2S, Codec)
    // This needs the actual codec driver implementation
    g_audio_pipeline = audio_pipeline_init();
    if (!g_audio_pipeline) {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline");
        // Handle error - maybe cannot proceed
    }

    // Initialize SIP Client
    // Pass necessary handles/configs
    g_sip_client = sip_client_init(app_event_group, SIP_REGISTERED_BIT, g_audio_pipeline);
     if (!g_sip_client) {
        ESP_LOGE(TAG, "Failed to initialize SIP client");
        // Handle error
    } else {
        // Link audio pipeline back to SIP client if needed for RTP data
         if (g_audio_pipeline) {
            audio_pipeline_set_sip_handle(g_audio_pipeline, g_sip_client);
         }
    }


    // Create application control task (optional, but good for managing overall state)
    xTaskCreate(app_control_task, "app_ctrl", 4096, NULL, 6, NULL);


    ESP_LOGI(TAG, "Initialization Complete. Waiting for Wi-Fi connection...");

    // Tasks for WiFi events, SIP, Audio are created within their respective init functions usually.
    // The system now runs on FreeRTOS tasks. app_main finishes here.
}
