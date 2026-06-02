#ifndef SIP_CLIENT_H
#define SIP_CLIENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "lwip/ip_addr.h"
#include "audio_pipeline.h"
#include "config_manager.h"

// Opaque handle for the SIP client instance
typedef struct sip_client_s* sip_client_handle_t;

// Callbacks for the application layer (optional)
typedef struct {
    void (*on_incoming_call)(const char* caller_uri, const char* call_id);
    void (*on_call_answered)(const char* call_id);
    void (*on_call_ended)(const char* call_id);
    void (*on_registration_status)(bool registered);
} sip_callbacks_t;

// Call States (Simplified)
typedef enum {
    SIP_CALL_STATE_IDLE,
    SIP_CALL_STATE_INVITING,   // Sent INVITE, waiting for response
    SIP_CALL_STATE_INCOMING,   // Received INVITE, waiting for user action (answer/reject)
    SIP_CALL_STATE_RINGING,    // Sent 180 Ringing
    SIP_CALL_STATE_CONNECTING, // Received 200 OK for INVITE / Sent 200 OK for INVITE
    SIP_CALL_STATE_ACTIVE,     // Call established (ACK sent/received)
    SIP_CALL_STATE_TERMINATING,// BYE sent or received
    SIP_CALL_STATE_ENDED
} sip_call_state_t;


// Function Prototypes
sip_client_handle_t sip_client_init(EventGroupHandle_t app_event_group, EventBits_t registered_event_bit, audio_pipeline_handle_t audio_handle, app_settings_t *settings);
esp_err_t sip_client_start_registration(sip_client_handle_t handle);
esp_err_t sip_client_initiate_call(sip_client_handle_t handle, const char* target_uri);
esp_err_t sip_client_answer_call(sip_client_handle_t handle);
esp_err_t sip_client_terminate_call(sip_client_handle_t handle); // Hang up active/incoming call
esp_err_t sip_client_register_callbacks(sip_client_handle_t handle, const sip_callbacks_t* callbacks);
sip_call_state_t sip_client_get_call_state(sip_client_handle_t handle);
esp_err_t sip_client_get_remote_rtp_info(sip_client_handle_t handle, ip_addr_t* remote_ip, uint16_t* remote_port);
uint16_t sip_client_get_local_rtp_port(sip_client_handle_t handle); // Function needed by audio pipeline

#endif // SIP_CLIENT_H
