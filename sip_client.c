// sip_client.h
#ifndef SIP_CLIENT_H
#define SIP_CLIENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "lwip/ip_addr.h"
#include "audio_pipeline.h" // Include audio handle

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
sip_client_handle_t sip_client_init(EventGroupHandle_t app_event_group, EventBits_t registered_event_bit, audio_pipeline_handle_t audio_handle);
esp_err_t sip_client_start_registration(sip_client_handle_t handle);
esp_err_t sip_client_initiate_call(sip_client_handle_t handle, const char* target_uri);
esp_err_t sip_client_answer_call(sip_client_handle_t handle);
esp_err_t sip_client_terminate_call(sip_client_handle_t handle); // Hang up active/incoming call
esp_err_t sip_client_register_callbacks(sip_client_handle_t handle, const sip_callbacks_t* callbacks);
sip_call_state_t sip_client_get_call_state(sip_client_handle_t handle);
esp_err_t sip_client_get_remote_rtp_info(sip_client_handle_t handle, ip_addr_t* remote_ip, uint16_t* remote_port);
uint16_t sip_client_get_local_rtp_port(sip_client_handle_t handle); // Function needed by audio pipeline

#endif // SIP_CLIENT_H

// sip_client.c - *** HIGHLY SIMPLIFIED - NEEDS FULL STATE MACHINE ***
#include <string.h>
#include "sip_client.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "freertos/timers.h"
#include "wifi_manager.h" // For get_my_ip()

static const char *TAG = "SIP_CLIENT";

// Structure for SIP client instance data
typedef struct sip_client_s {
    EventGroupHandle_t event_group;
    EventBits_t registered_bit;
    audio_pipeline_handle_t audio; // Handle to audio system
    sip_callbacks_t app_callbacks; // Application callbacks
    TimerHandle_t registration_timer;
    TaskHandle_t sip_task_handle;

    int sip_socket;
    struct sockaddr_in server_addr;
    struct sockaddr_in local_addr; // Our local address for SIP
    struct sockaddr_in remote_rtp_addr; // Remote peer's RTP address/port from SDP

    char local_ip_str[16];
    char public_ip_str[16];    // For STUN (optional)
    uint16_t local_sip_port;
    uint16_t local_rtp_port;
    uint16_t public_sip_port;  // For STUN (optional)

    char user[64];
    char password[64];
    char domain[128];
    char display_name[64];

    volatile bool is_registered;
    volatile sip_call_state_t call_state;
    uint32_t current_cseq;
    char current_call_id[64];
    char current_from_tag[32];
    char current_to_tag[32];   // Received from peer in responses/requests
    char current_remote_uri[128]; // URI of the remote party in active call

    // Add fields for ongoing transaction management (e.g., last request, timeouts)
    // ...

} sip_client_t;


// --- Forward Declarations of Static Helper Functions ---
static void sip_task(void *pvParameters);
static esp_err_t create_sip_socket(sip_client_t *client);
static void send_register(sip_client_t *client, bool initial_registration);
static void send_invite(sip_client_t *client, const char *target_uri);
static void send_ack(sip_client_t *client);
static void send_bye(sip_client_t *client);
static void send_sip_response(sip_client_t *client, int status_code, const char *reason_phrase, const char* to_tag); // Simplified
static void process_incoming_sip(sip_client_t *client, char *buffer, int len, struct sockaddr_in *remote_addr);
static void registration_timer_callback(TimerHandle_t xTimer);
static bool parse_sdp(const char *sdp_body, ip_addr_t *remote_ip, uint16_t *remote_port); // Simplified SDP parser
static int generate_sdp(sip_client_t *client, char *buffer, size_t buffer_len); // Generates our SDP offer/answer

// --- Public Functions ---

sip_client_handle_t sip_client_init(EventGroupHandle_t app_event_group, EventBits_t registered_event_bit, audio_pipeline_handle_t audio_handle) {
    ESP_LOGI(TAG, "Initializing SIP Client...");
    sip_client_t *client = (sip_client_t *)calloc(1, sizeof(sip_client_t));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate memory for SIP client");
        return NULL;
    }

    client->event_group = app_event_group;
    client->registered_bit = registered_event_bit;
    client->audio = audio_handle;
    client->sip_socket = -1;
    client->is_registered = false;
    client->call_state = SIP_CALL_STATE_IDLE;
    client->current_cseq = 1; // Initial CSeq
    client->local_sip_port = SIP_LOCAL_PORT;
    client->local_rtp_port = RTP_LOCAL_PORT_BASE; // Simple assignment for now

    // Copy configuration
    strncpy(client->user, SIP_USER, sizeof(client->user) - 1);
    strncpy(client->password, SIP_PASSWORD, sizeof(client->password) - 1);
    strncpy(client->domain, SIP_DOMAIN, sizeof(client->domain) - 1);
    strncpy(client->display_name, SIP_DISPLAY_NAME, sizeof(client->display_name) - 1);


    // Resolve server address (basic DNS lookup)
    ip_addr_t target_addr;
    err_t err = dns_gethostbyname(SIP_SERVER_IP, &target_addr, NULL, NULL); // Blocking! Consider async DNS.
    if (err != ERR_OK) {
         ESP_LOGE(TAG, "DNS lookup failed for %s: %d", SIP_SERVER_IP, err);
         // Handle error - maybe try IP directly if SIP_SERVER_IP is an IP string
         // For simplicity, trying direct IP conversion if DNS fails
         if (!ipaddr_aton(SIP_SERVER_IP, &target_addr)) {
             ESP_LOGE(TAG, "Failed to resolve server address.");
             free(client);
             return NULL;
         }
    }

    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons(SIP_SERVER_PORT);
    client->server_addr.sin_addr.s_addr = ip_addr_get_ip4_u32(&target_addr);


    // Create registration timer
    client->registration_timer = xTimerCreate("RegTimer", pdMS_TO_TICKS(SIP_REGISTRATION_EXPIRY * 1000 * 0.9), // Register slightly before expiry
                                              pdFALSE, // One-shot timer initially
                                              (void *)client,
                                              registration_timer_callback);
    if (!client->registration_timer) {
        ESP_LOGE(TAG, "Failed to create registration timer");
        free(client);
        return NULL;
    }

    // Create SIP processing task
    if (xTaskCreate(sip_task, "sip_task", SIP_TASK_STACK_SIZE, client, SIP_TASK_PRIORITY, &client->sip_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SIP task");
        xTimerDelete(client->registration_timer, portMAX_DELAY);
        free(client);
        return NULL;
    }

    ESP_LOGI(TAG, "SIP Client Initialized.");
    return client;
}

esp_err_t sip_client_start_registration(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client) return ESP_ERR_INVALID_ARG;

    // Signal the task to start the process (if needed, or just call send_register directly)
    // For simplicity here, we assume the task will handle it on startup after IP is acquired.
     ESP_LOGI(TAG, "Registration process will be initiated by SIP task.");
    // Or: send_register(client, true); // If called after IP is known and socket is ready

    return ESP_OK;
}


esp_err_t sip_client_initiate_call(sip_client_handle_t handle, const char* target_uri) {
     sip_client_t *client = (sip_client_t *)handle;
     if (!client || !target_uri || client->call_state != SIP_CALL_STATE_IDLE || !client->is_registered) {
         ESP_LOGE(TAG, "Cannot initiate call: Invalid state (%d) or not registered (%d)", client->call_state, client->is_registered);
         return ESP_FAIL;
     }
     strncpy(client->current_remote_uri, target_uri, sizeof(client->current_remote_uri) - 1);
     client->call_state = SIP_CALL_STATE_INVITING;
     // Generate Call-ID and From-tag
     sprintf(client->current_call_id, "%lx-%lx", esp_random(), esp_random());
     sprintf(client->current_from_tag, "%lx", esp_random());
     client->current_to_tag[0] = '\0'; // To tag is empty initially

     ESP_LOGI(TAG, "Initiating call to %s, Call-ID: %s", target_uri, client->current_call_id);
     send_invite(client, target_uri);
     // TODO: Start INVITE transaction timer
     return ESP_OK;
}

esp_err_t sip_client_answer_call(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client || client->call_state != SIP_CALL_STATE_INCOMING) {
        ESP_LOGE(TAG, "Cannot answer call: Invalid state (%d)", client->call_state);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Answering incoming call, Call-ID: %s", client->current_call_id);

    // Send 200 OK response to the INVITE
    send_sip_response(client, 200, "OK", client->current_to_tag); // Need To tag from INVITE here

    client->call_state = SIP_CALL_STATE_CONNECTING; // Wait for ACK
    // NOTE: Audio should typically start *after* receiving ACK for robustness,
    // but some implementations start on sending 200 OK.
    // Start audio requires interaction with audio_pipeline
    // audio_pipeline_start(client->audio, &client->remote_rtp_addr, client->local_rtp_port);

    return ESP_OK;
}

esp_err_t sip_client_terminate_call(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client) return ESP_ERR_INVALID_ARG;

    if (client->call_state == SIP_CALL_STATE_ACTIVE ||
        client->call_state == SIP_CALL_STATE_CONNECTING ||
        client->call_state == SIP_CALL_STATE_RINGING ||
        client->call_state == SIP_CALL_STATE_INCOMING ||
         client->call_state == SIP_CALL_STATE_INVITING)
    {
        ESP_LOGI(TAG, "Terminating call, Call-ID: %s, State: %d", client->current_call_id, client->call_state);
        send_bye(client); // Send BYE if call was active/connecting
        // If INVITE was just sent/received, might need CANCEL instead/also. Simplified here.
        client->call_state = SIP_CALL_STATE_TERMINATING;

        // Stop audio
        if (client->audio) {
            audio_pipeline_stop(client->audio);
        }

        // TODO: Start BYE transaction timer or cleanup immediately if needed
        client->call_state = SIP_CALL_STATE_IDLE; // Simplified state transition
        client->current_call_id[0] = '\0';
        // Trigger callback
        if (client->app_callbacks.on_call_ended) {
             client->app_callbacks.on_call_ended(client->current_call_id); // Pass relevant ID
        }

    } else {
        ESP_LOGW(TAG, "No active call to terminate. State: %d", client->call_state);
        return ESP_FAIL;
    }
    return ESP_OK;
}


uint16_t sip_client_get_local_rtp_port(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
     return client ? client->local_rtp_port : 0;
}

esp_err_t sip_client_get_remote_rtp_info(sip_client_handle_t handle, ip_addr_t* remote_ip, uint16_t* remote_port) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client || !remote_ip || !remote_port || client->call_state < SIP_CALL_STATE_CONNECTING) {
        return ESP_FAIL; // No active call or info not yet available
    }
    memcpy(remote_ip, &client->remote_rtp_addr.sin_addr, sizeof(struct in_addr));
    *remote_port = ntohs(client->remote_rtp_addr.sin_port);
    return ESP_OK;
}


sip_call_state_t sip_client_get_call_state(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    return client ? client->call_state : SIP_CALL_STATE_IDLE;
}

// --- Private Task and Helper Functions ---

static void sip_task(void *pvParameters) {
    sip_client_t *client = (sip_client_t *)pvParameters;
    char rx_buffer[2048]; // Buffer for incoming SIP messages
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "SIP Task Started. Waiting for IP address...");

    // Wait for Wi-Fi connection and IP address
    EventBits_t bits = xEventGroupWaitBits(client->event_group, IP_ACQUIRED_BIT,
                                         pdFALSE, // Don't clear the bit
                                         pdTRUE,  // Wait for the bit to be set
                                         portMAX_DELAY);

    if (!(bits & IP_ACQUIRED_BIT)) {
        ESP_LOGE(TAG, "Failed to get IP address. SIP task exiting.");
        vTaskDelete(NULL);
        return;
    }
     ESP_LOGI(TAG, "IP address acquired.");

    // Get our local IP
    esp_ip4_addr_t my_ip;
    if (get_my_ip(&my_ip) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get local IP address. SIP task exiting.");
        vTaskDelete(NULL);
        return;
    }
    sprintf(client->local_ip_str, IPSTR, IP2STR(&my_ip));
    ESP_LOGI(TAG, "Using local IP: %s", client->local_ip_str);


    // Create and bind the SIP socket
    if (create_sip_socket(client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SIP socket. Task exiting.");
        vTaskDelete(NULL);
        return;
    }

    // --- Start Initial Registration ---
    send_register(client, true); // Send first REGISTER message
    // Start timer only after successful REGISTER response normally, simplified here.
    // xTimerStart(client->registration_timer, 0);


    ESP_LOGI(TAG, "SIP Task listening on UDP port %d", client->local_sip_port);

    while (1) {
        // Use select for timeout and non-blocking read
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client->sip_socket, &readfds);
        struct timeval tv;
        tv.tv_sec = 1; // Timeout of 1 second
        tv.tv_usec = 0;

        int s = select(client->sip_socket + 1, &readfds, NULL, NULL, &tv);

        if (s < 0) {
            ESP_LOGE(TAG, "Select error: %d, errno: %d", s, errno);
            // Handle socket error, maybe recreate socket?
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        } else if (s == 0) {
            // Timeout - opportunity to check timers or perform periodic tasks
            // ESP_LOGD(TAG, "Select timeout");
            // Check registration status, resend if needed
            if (!client->is_registered) {
                 // Simple retry logic
                 // send_register(client, true); // Re-send initial REGISTER
                 // vTaskDelay(pdMS_TO_TICKS(SIP_RETRY_INTERVAL_MS));
            }
        } else {
            // Data available on socket
            if (FD_ISSET(client->sip_socket, &readfds)) {
                int len = recvfrom(client->sip_socket, rx_buffer, sizeof(rx_buffer) - 1, 0,
                                   (struct sockaddr *)&source_addr, &socklen);
                if (len > 0) {
                    rx_buffer[len] = '\0'; // Null-terminate the received data
                    ESP_LOGD(TAG, "Received %d bytes from %s:%d", len,
                             inet_ntoa(source_addr.sin_addr), ntohs(source_addr.sin_port));
                    ESP_LOGV(TAG, "SIP RX:\n%s", rx_buffer); // Verbose logging

                    // Process the incoming SIP message
                    process_incoming_sip(client, rx_buffer, len, &source_addr);

                } else if (len < 0) {
                    ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                    // Handle error
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        }
         // Add periodic checks (e.g., keep-alives via OPTIONS if needed)
         // vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to yield
    } // End while(1)

    // Cleanup (normally not reached)
    ESP_LOGW(TAG, "SIP Task exiting.");
    close(client->sip_socket);
    client->sip_socket = -1;
    xTimerDelete(client->registration_timer, portMAX_DELAY);
    vTaskDelete(NULL);
}


static esp_err_t create_sip_socket(sip_client_t *client) {
    client->sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client->sip_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(client->local_sip_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to any local IP

    int err = bind(client->sip_socket, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(client->sip_socket);
        client->sip_socket = -1;
        return ESP_FAIL;
    }

     // Store local address info after binding (port might be different if 0 was used)
    socklen_t addrlen = sizeof(client->local_addr);
    getsockname(client->sip_socket, (struct sockaddr*)&client->local_addr, &addrlen);
    client->local_sip_port = ntohs(client->local_addr.sin_port); // Update actual port used
    // Get local IP properly here as well.

    ESP_LOGI(TAG, "SIP Socket created and bound to port %d", client->local_sip_port);
    return ESP_OK;
}

static void registration_timer_callback(TimerHandle_t xTimer) {
    sip_client_t *client = (sip_client_t *)pvTimerGetTimerID(xTimer);
    ESP_LOGI(TAG, "Registration timer expired. Sending REGISTER.");
    if (client && client->sip_socket >= 0) {
        send_register(client, false); // Re-registration
    }
}

// --- SIP Message Generation (Placeholders - Require Full Implementation) ---

static void send_register(sip_client_t *client, bool initial_registration) {
    char buffer[1024]; // Adjust size as needed
    char branch[32];
    sprintf(branch, "z9hG4bK%lx", esp_random()); // Basic branch generation

    // TODO: Add Digest Authentication handling if server sends 401/407
    // This requires parsing WWW-Authenticate/Proxy-Authenticate headers
    // and generating Authorization/Proxy-Authorization headers.
    // This example sends a basic REGISTER without Auth.

    int len = snprintf(buffer, sizeof(buffer),
        "REGISTER sip:%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n" // rport for NAT traversal
        "Max-Forwards: 70\r\n"
        "From: \"%s\" <sip:%s@%s>;tag=%lx\r\n"
        "To: \"%s\" <sip:%s@%s>\r\n"
        "Call-ID: %lx-%lx\r\n" // Call-ID should be unique per registration attempt
        "CSeq: %lu REGISTER\r\n"
        "Contact: <sip:%s@%s:%d>\r\n" // IMPORTANT: Use correct local IP/Port
        "Expires: %d\r\n"
        "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE\r\n" // Advertise supported methods
        "User-Agent: ESP32-SIPClient/1.0\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        client->domain,
        client->local_ip_str, client->local_sip_port, branch, // Use discovered public IP/Port if using STUN
        client->display_name, client->user, client->domain, esp_random(), // From tag unique per REGISTER
        client->display_name, client->user, client->domain,
        esp_random(), esp_random(), // Unique Call-ID for registration
        client->current_cseq++, // Increment CSeq
        client->user, client->local_ip_str, client->local_sip_port, // Use correct IP/Port
        initial_registration ? SIP_REGISTRATION_EXPIRY : SIP_REGISTRATION_EXPIRY // Use 0 to unregister
        // Add Content-Length: 0 for REGISTER
    );

    if (len > 0 && len < sizeof(buffer)) {
         ESP_LOGI(TAG, "Sending REGISTER (CSeq: %lu)", client->current_cseq - 1);
         ESP_LOGV(TAG, "SIP TX:\n%s", buffer);
         sendto(client->sip_socket, buffer, len, 0, (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
         // TODO: Start REGISTER transaction timer
    } else {
         ESP_LOGE(TAG, "Buffer overflow creating REGISTER message");
    }
}


static int generate_sdp(sip_client_t *client, char *buffer, size_t buffer_len) {
     // Basic SDP offering G.711 u-law on the configured RTP port
     // TODO: Get actual local IP if different from SIP signaling IP
     return snprintf(buffer, buffer_len,
                "v=0\r\n"
                "o=%s %lu %lu IN IP4 %s\r\n" // user, session id, version, network type, addr type, addr
                "s=ESP32 Call\r\n"
                "c=IN IP4 %s\r\n"            // connection network type, addr type, addr
                "t=0 0\r\n"
                "m=audio %d RTP/AVP %d\r\n"   // media type, port, proto, payload type(s)
                "a=rtpmap:%d PCMU/%d\r\n"    // payload type, encoding name, clock rate
                "a=ptime:%d\r\n"              // packet time
                "a=sendrecv\r\n",             // or sendonly/recvonly
                client->user, esp_random(), esp_random(), client->local_ip_str, // session origin
                client->local_ip_str,                                           // connection address
                client->local_rtp_port, AUDIO_CODEC_PAYLOAD_TYPE,               // media line (port, PT)
                AUDIO_CODEC_PAYLOAD_TYPE, AUDIO_SAMPLE_RATE,                    // rtpmap line
                AUDIO_FRAME_MS                                                  // ptime line
            );
}

static void send_invite(sip_client_t *client, const char *target_uri) {
    char buffer[2048];
    char sdp_buffer[512];
    char branch[32];
    sprintf(branch, "z9hG4bK%lx", esp_random());

    int sdp_len = generate_sdp(client, sdp_buffer, sizeof(sdp_buffer));
    if (sdp_len <= 0) {
        ESP_LOGE(TAG, "Failed to generate SDP");
        client->call_state = SIP_CALL_STATE_IDLE;
        return;
    }

    int len = snprintf(buffer, sizeof(buffer),
        "INVITE %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: \"%s\" <sip:%s@%s>;tag=%s\r\n" // Use generated From tag
        "To: <%s>\r\n"                         // To URI, no tag initially
        "Call-ID: %s\r\n"                      // Use generated Call-ID
        "CSeq: %lu INVITE\r\n"
        "Contact: <sip:%s@%s:%d>\r\n"
        "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE\r\n"
        "Content-Type: application/sdp\r\n"
        "User-Agent: ESP32-SIPClient/1.0\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s", // SDP body
        target_uri,
        client->local_ip_str, client->local_sip_port, branch,
        client->display_name, client->user, client->domain, client->current_from_tag,
        target_uri,
        client->current_call_id,
        client->current_cseq++,
        client->user, client->local_ip_str, client->local_sip_port,
        sdp_len,
        sdp_buffer
    );

    if (len > 0 && len < sizeof(buffer)) {
         ESP_LOGI(TAG, "Sending INVITE (CSeq: %lu)", client->current_cseq - 1);
         ESP_LOGV(TAG, "SIP TX:\n%s", buffer);
         sendto(client->sip_socket, buffer, len, 0, (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
         // TODO: Start INVITE transaction timer
    } else {
         ESP_LOGE(TAG, "Buffer overflow creating INVITE message");
         client->call_state = SIP_CALL_STATE_IDLE; // Revert state
    }
}

static void send_ack(sip_client_t *client) {
     // ACK is sent in response to a final (2xx) response to INVITE
     // It reuses Via, From, To, Call-ID, CSeq (number only) from the INVITE.
     // The To header *must* include the tag received in the 200 OK.
     // The Request-URI is the Contact URI received in the 200 OK. (Important!)
     ESP_LOGW(TAG, "send_ack: NOT IMPLEMENTED"); // Placeholder
     // Build ACK message...
     // sendto(...)
}

static void send_bye(sip_client_t *client) {
     if (client->call_state < SIP_CALL_STATE_CONNECTING) return; // No call to terminate

     char buffer[1024];
     char branch[32];
     sprintf(branch, "z9hG4bK%lx", esp_random());

     int len = snprintf(buffer, sizeof(buffer),
         "BYE %s SIP/2.0\r\n" // Request-URI is the remote target's URI (from To/From of established dialog)
         "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
         "Max-Forwards: 70\r\n"
         "From: \"%s\" <sip:%s@%s>;tag=%s\r\n" // Our From tag
         "To: <%s>;tag=%s\r\n"                 // Remote URI and To tag
         "Call-ID: %s\r\n"
         "CSeq: %lu BYE\r\n"
         "User-Agent: ESP32-SIPClient/1.0\r\n"
         "Content-Length: 0\r\n"
         "\r\n",
         client->current_remote_uri, // Use the stored remote URI
         client->local_ip_str, client->local_sip_port, branch,
         client->display_name, client->user, client->domain, client->current_from_tag,
         client->current_remote_uri, client->current_to_tag, // Use remote URI and tag
         client->current_call_id,
         client->current_cseq++, // Increment CSeq for new request
         // Content-Length: 0
         ""
     );

     if (len > 0 && len < sizeof(buffer)) {
          ESP_LOGI(TAG, "Sending BYE (CSeq: %lu)", client->current_cseq - 1);
          ESP_LOGV(TAG, "SIP TX:\n%s", buffer);
          // Determine destination: usually the remote Contact URI from INVITE/200 OK, or just server
          sendto(client->sip_socket, buffer, len, 0, (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
          // TODO: Start BYE transaction timer
     } else {
          ESP_LOGE(TAG, "Buffer overflow creating BYE message");
     }
}

static void send_sip_response(sip_client_t *client, int status_code, const char *reason_phrase, const char* to_tag) {
    // This function needs context from the received request (Via, From, To, Call-ID, CSeq)
    // This is a simplified placeholder showing the concept.
    ESP_LOGW(TAG, "send_sip_response (%d %s): NOT FULLY IMPLEMENTED", status_code, reason_phrase);
    // Example: Sending 200 OK to INVITE
    // char buffer[1024];
    // char sdp_buffer[512];
    // int sdp_len = generate_sdp(client, sdp_buffer, sizeof(sdp_buffer));
    // int len = snprintf(buffer, sizeof(buffer),
    //      "SIP/2.0 %d %s\r\n"
    //      "Via: ...\r\n" // Copy Via header(s) from request
    //      "From: ...\r\n" // Copy From header from request
    //      "To: ...;tag=%s\r\n" // Copy To header from request, ADD our tag
    //      "Call-ID: ...\r\n" // Copy Call-ID from request
    //      "CSeq: ...\r\n" // Copy CSeq from request
    //      "Contact: <sip:%s@%s:%d>\r\n"
    //      "Content-Type: application/sdp\r\n"
    //      "Content-Length: %d\r\n"
    //      "\r\n"
    //      "%s",
    //      status_code, reason_phrase,
    //      to_tag ? to_tag : "", // Add our generated tag
    //      client->user, client->local_ip_str, client->local_sip_port,
    //      (sdp_len > 0) ? sdp_len : 0,
    //      (sdp_len > 0) ? sdp_buffer : ""
    // );
    // sendto(...) directed back to the source address from the Via header (or source IP/Port)
}


// --- SIP Message Parsing (Placeholders - Require Full Implementation) ---

static bool parse_header(const char *msg, const char *hdr_name, char *out_buf, size_t out_len) {
    // Very basic header parser - find header name, copy value until \r\n
    const char *hdr_start = strcasestr(msg, hdr_name);
    if (!hdr_start) return false;

    const char *value_start = hdr_start + strlen(hdr_name);
    while (*value_start == ' ' || *value_start == ':') { // Skip colon and leading spaces
        value_start++;
    }
    const char *value_end = strstr(value_start, "\r\n");
    if (!value_end) return false; // Malformed

    size_t value_len = value_end - value_start;
    if (value_len >= out_len) {
        value_len = out_len - 1; // Prevent overflow
    }

    strncpy(out_buf, value_start, value_len);
    out_buf[value_len] = '\0';
    // Trim trailing whitespace if necessary
    // ...
    return true;
}

static bool parse_sdp(const char *sdp_body, ip_addr_t *remote_ip, uint16_t *remote_port) {
     if (!sdp_body) return false;
     ESP_LOGD(TAG, "Parsing SDP:\n%s", sdp_body);

     const char *c_line = strstr(sdp_body, "\nc="); // Find connection line (prefer after \n)
     if (!c_line) c_line = strstr(sdp_body, "\rc=");
     if (!c_line) c_line = strstr(sdp_body, "c="); // Fallback

     const char *m_line = strstr(sdp_body, "\nm=audio"); // Find media line
      if (!m_line) m_line = strstr(sdp_body, "\rm=audio");
      if (!m_line) m_line = strstr(sdp_body, "m=audio");


     if (c_line) {
         char ip_str[64];
         // Basic parse: c=IN IP4 <address>
         if (sscanf(c_line, "%*[^ ] IN IP4 %63s", ip_str) == 1) {
              if (!ipaddr_aton(ip_str, remote_ip)) {
                 ESP_LOGE(TAG, "Failed to parse IP address from SDP c-line: %s", ip_str);
                 // Handle error or try parsing from m-line if needed
              } else {
                   ESP_LOGI(TAG, "SDP parsed remote IP: %s", ipaddr_ntoa(remote_ip));
              }
         } else {
              ESP_LOGW(TAG, "Could not parse IP from SDP c-line: %s", c_line);
         }
     } else {
         ESP_LOGW(TAG, "SDP 'c=' line not found.");
         // Maybe default to sender's IP if c= line is missing? Requires sender IP passed in.
         return false; // Often required
     }

     if (m_line) {
         unsigned int port = 0;
         unsigned int payload_type = 0;
         // Basic parse: m=audio <port> RTP/AVP <payload_type> ...
         if (sscanf(m_line, "%*[^ ] %u RTP/AVP %u", &port, &payload_type) >= 1) { // Payload type is optional here
             *remote_port = (uint16_t)port;
             ESP_LOGI(TAG, "SDP parsed remote port: %u, Payload Type: %u", *remote_port, payload_type);
             // TODO: Validate payload type against our supported codecs (e.g., == AUDIO_CODEC_PAYLOAD_TYPE)
             if (payload_type != AUDIO_CODEC_PAYLOAD_TYPE) {
                 ESP_LOGW(TAG, "Unsupported payload type %u received in SDP.", payload_type);
                 // Cannot establish call with this media format
                 return false;
             }
         } else {
             ESP_LOGW(TAG, "Could not parse port from SDP m-line: %s", m_line);
             return false; // Port is essential
         }
     } else {
         ESP_LOGW(TAG, "SDP 'm=audio' line not found.");
         return false; // Required
     }

     return (*remote_port != 0); // Basic validation
}


static void process_incoming_sip(sip_client_t *client, char *buffer, int len, struct sockaddr_in *remote_addr) {
    // VERY Simplified Parser - assumes well-formed messages
    char method[32] = {0};
    char cseq_hdr[64] = {0};
    char call_id_hdr[128] = {0};
    char from_hdr[256] = {0};
    char to_hdr[256] = {0};
    char via_hdr[256] = {0}; // Should handle multiple Via headers
    char contact_hdr[256] = {0};
    char content_type_hdr[64] = {0};
    uint32_t cseq_num = 0;
    char cseq_method[32] = {0};
    int status_code = 0;

    // Check if it's a response or request
    if (strncmp(buffer, "SIP/2.0", 7) == 0) {
        // --- It's a Response ---
        if (sscanf(buffer, "SIP/2.0 %d", &status_code) != 1) {
            ESP_LOGW(TAG, "Could not parse status code from response");
            return;
        }
        ESP_LOGD(TAG, "Received Response: %d", status_code);

        parse_header(buffer, "CSeq", cseq_hdr, sizeof(cseq_hdr));
        parse_header(buffer, "Call-ID", call_id_hdr, sizeof(call_id_hdr));
        parse_header(buffer, "From", from_hdr, sizeof(from_hdr)); // Needed to match transaction
        parse_header(buffer, "To", to_hdr, sizeof(to_hdr));     // Needed to match transaction

        sscanf(cseq_hdr, "%u %31s", &cseq_num, cseq_method);

        // --- Response Handling Logic (Highly Simplified) ---
        if (strcasecmp(cseq_method, "REGISTER") == 0) {
            if (status_code == 200) {
                ESP_LOGI(TAG, "Registration successful!");
                client->is_registered = true;
                xEventGroupSetBits(client->event_group, client->registered_bit);
                // Extract expiration if provided, restart timer
                xTimerStart(client->registration_timer, portMAX_DELAY);
                 if (client->app_callbacks.on_registration_status) {
                     client->app_callbacks.on_registration_status(true);
                 }
            } else if (status_code == 401 || status_code == 407) {
                 ESP_LOGW(TAG, "Registration failed: Authentication required (%d). Digest Auth not implemented.", status_code);
                 // TODO: Implement Digest Authentication: parse WWW-Authenticate/Proxy-Authenticate, generate response
                 client->is_registered = false;
                 xEventGroupClearBits(client->event_group, client->registered_bit);
                 xTimerStop(client->registration_timer, portMAX_DELAY); // Stop timer
                 if (client->app_callbacks.on_registration_status) {
                      client->app_callbacks.on_registration_status(false);
                 }
                 // Retry after delay? Maybe stop retrying if Auth fails repeatedly.
            } else {
                ESP_LOGE(TAG, "Registration failed with status code %d", status_code);
                 client->is_registered = false;
                 xEventGroupClearBits(client->event_group, client->registered_bit);
                 xTimerStop(client->registration_timer, portMAX_DELAY);
                 if (client->app_callbacks.on_registration_status) {
                      client->app_callbacks.on_registration_status(false);
                 }
                 // Retry after delay?
            }
        } else if (strcasecmp(cseq_method, "INVITE") == 0) {
             // Check Call-ID, From Tag, To Tag (if present) match our ongoing call
             if (client->call_state == SIP_CALL_STATE_INVITING && strcmp(client->current_call_id, call_id_hdr) == 0) {
                  if (status_code >= 100 && status_code < 200) { // Provisional response (180 Ringing, 183 Session Progress)
                       ESP_LOGI(TAG, "Received provisional response to INVITE: %d", status_code);
                       // Extract To tag if present
                       // TODO: Handle 183 with SDP (early media)
                       if (status_code == 180) client->call_state = SIP_CALL_STATE_RINGING; // Or similar state
                  } else if (status_code == 200) { // Call Accepted
                       ESP_LOGI(TAG, "INVITE accepted (200 OK)");
                       client->call_state = SIP_CALL_STATE_CONNECTING;
                       // Extract To tag from response - IMPORTANT
                       // char *tag_ptr = strstr(to_hdr, ";tag="); if (tag_ptr) strncpy(client->current_to_tag, tag_ptr + 5, ...);

                       // Parse SDP from the 200 OK body
                       char *sdp_body = strstr(buffer, "\r\n\r\n");
                       if (sdp_body) {
                           sdp_body += 4;
                           ip_addr_t remote_ip;
                           uint16_t remote_port;
                           if (parse_sdp(sdp_body, &remote_ip, &remote_port)) {
                                client->remote_rtp_addr.sin_family = AF_INET;
                                client->remote_rtp_addr.sin_port = htons(remote_port);
                                memcpy(&client->remote_rtp_addr.sin_addr, &remote_ip, sizeof(struct in_addr));

                                // Send ACK
                                send_ack(client);
                                client->call_state = SIP_CALL_STATE_ACTIVE; // Move to active after sending ACK

                                // Start audio pipeline
                                if(client->audio) {
                                     audio_pipeline_start(client->audio, client->local_rtp_port);
                                }

                                // Trigger callback
                                if (client->app_callbacks.on_call_answered) {
                                    client->app_callbacks.on_call_answered(client->current_call_id);
                                }

                           } else {
                                ESP_LOGE(TAG, "Failed to parse SDP in 200 OK. Terminating call.");
                                send_bye(client); // Or maybe CANCEL if ACK not sent? Risky. BYE is safer.
                                client->call_state = SIP_CALL_STATE_IDLE;
                           }
                       } else {
                           ESP_LOGE(TAG, "No SDP body found in 200 OK. Terminating call.");
                           send_bye(client);
                           client->call_state = SIP_CALL_STATE_IDLE;
                       }
                  } else { // Call Rejected or Error (4xx, 5xx, 6xx)
                       ESP_LOGW(TAG, "INVITE failed/rejected with status code %d", status_code);
                       client->call_state = SIP_CALL_STATE_ENDED; // Or IDLE
                       // Send ACK for 4xx-6xx responses too!
                       send_ack(client);
                       client->call_state = SIP_CALL_STATE_IDLE; // Back to idle after ACK
                       client->current_call_id[0] = '\0';
                       // Trigger callback
                       if (client->app_callbacks.on_call_ended) {
                           client->app_callbacks.on_call_ended(client->current_call_id); // Pass relevant ID
                       }
                  }
             }
        } else if (strcasecmp(cseq_method, "BYE") == 0) {
             // Response to our BYE
             if (status_code == 200) {
                 ESP_LOGI(TAG, "Received 200 OK for BYE. Call terminated.");
                 client->call_state = SIP_CALL_STATE_IDLE; // Final state
                 client->current_call_id[0] = '\0';
                 // Callback already triggered when sending BYE usually
             } else {
                 ESP_LOGW(TAG, "Received non-200 response (%d) for BYE.", status_code);
                 // Maybe retry BYE? Or just force state to IDLE.
                 client->call_state = SIP_CALL_STATE_IDLE;
                 client->current_call_id[0] = '\0';
             }
        }
         // TODO: Handle responses to OPTIONS, CANCEL, etc.

    } else {
        // --- It's a Request ---
        if (sscanf(buffer, "%31s", method) != 1) {
            ESP_LOGW(TAG, "Could not parse method from request");
            return;
        }
        ESP_LOGD(TAG, "Received Request: %s", method);

        parse_header(buffer, "CSeq", cseq_hdr, sizeof(cseq_hdr));
        parse_header(buffer, "Call-ID", call_id_hdr, sizeof(call_id_hdr));
        parse_header(buffer, "From", from_hdr, sizeof(from_hdr));
        parse_header(buffer, "To", to_hdr, sizeof(to_hdr));
        parse_header(buffer, "Via", via_hdr, sizeof(via_hdr)); // Get top Via for response routing
        parse_header(buffer, "Contact", contact_hdr, sizeof(contact_hdr));

        // --- Request Handling Logic (Highly Simplified) ---
        if (strcmp(method, "INVITE") == 0) {
            if (client->call_state != SIP_CALL_STATE_IDLE) {
                 ESP_LOGW(TAG, "Received INVITE while already in a call. Sending 486 Busy Here.");
                 send_sip_response(client, 486, "Busy Here", NULL); // Need to extract details from INVITE to send proper response
                 return;
            }

            // Store call details
            strncpy(client->current_call_id, call_id_hdr, sizeof(client->current_call_id) - 1);
            // Extract remote URI from From header
            // Extract From tag
            // Extract To tag (usually none in initial INVITE)
            // Generate our To tag for the response
            sprintf(client->current_to_tag, "%lx", esp_random());


             // Parse SDP
             char *sdp_body = strstr(buffer, "\r\n\r\n");
             if (sdp_body) {
                  sdp_body += 4;
                  ip_addr_t remote_ip;
                  uint16_t remote_port;
                  if (parse_sdp(sdp_body, &remote_ip, &remote_port)) {
                       client->remote_rtp_addr.sin_family = AF_INET;
                       client->remote_rtp_addr.sin_port = htons(remote_port);
                       memcpy(&client->remote_rtp_addr.sin_addr, &remote_ip, sizeof(struct in_addr));

                       ESP_LOGI(TAG, "Incoming INVITE received from %s, Call-ID: %s", from_hdr, call_id_hdr);
                       client->call_state = SIP_CALL_STATE_INCOMING;

                       // Send 100 Trying immediately (optional but good practice)
                       send_sip_response(client, 100, "Trying", NULL);

                       // Send 180 Ringing
                       send_sip_response(client, 180, "Ringing", client->current_to_tag); // Add our To tag

                       // Notify application layer
                       if (client->app_callbacks.on_incoming_call) {
                           // Extract caller URI properly from From header
                           char caller_uri[128];
                           // sscanf(from_hdr, "%*[^<]<%127[^>]>", caller_uri); // Example extraction
                           snprintf(caller_uri, sizeof(caller_uri),"%s", from_hdr); // Simplification
                           client->app_callbacks.on_incoming_call(caller_uri, client->current_call_id);
                       }

                  } else {
                       ESP_LOGE(TAG, "Failed to parse SDP in incoming INVITE. Sending 415 Unsupported Media Type.");
                       send_sip_response(client, 415, "Unsupported Media Type", NULL);
                  }
             } else {
                 ESP_LOGE(TAG, "INVITE received without SDP body. Sending 400 Bad Request.");
                 send_sip_response(client, 400, "Bad Request", NULL);
             }

        } else if (strcmp(method, "ACK") == 0) {
             // Received ACK for our 200 OK to an INVITE
             if (client->call_state == SIP_CALL_STATE_CONNECTING) {
                  ESP_LOGI(TAG, "Received ACK for 200 OK. Call is now ACTIVE.");
                  client->call_state = SIP_CALL_STATE_ACTIVE;
                  // Start audio if it wasn't started on sending 200 OK
                  if(client->audio) {
                       audio_pipeline_start(client->audio, client->local_rtp_port);
                  }
                  // Trigger answered callback if not done earlier
                  if (client->app_callbacks.on_call_answered) {
                      client->app_callbacks.on_call_answered(client->current_call_id);
                  }
             } else {
                  ESP_LOGW(TAG, "Received unexpected ACK in state %d", client->call_state);
             }

        } else if (strcmp(method, "BYE") == 0) {
             // Peer wants to end the call
             if (client->call_state >= SIP_CALL_STATE_CONNECTING && client->call_state <= SIP_CALL_STATE_ACTIVE) {
                 ESP_LOGI(TAG, "Received BYE request. Terminating call.");
                 client->call_state = SIP_CALL_STATE_TERMINATING; // Intermediate state

                 // Stop audio pipeline
                 if (client->audio) {
                     audio_pipeline_stop(client->audio);
                 }

                 // Send 200 OK response to the BYE
                 send_sip_response(client, 200, "OK", NULL); // Need context from BYE req

                 client->call_state = SIP_CALL_STATE_IDLE; // Final state after response
                 client->current_call_id[0] = '\0';
                 // Trigger callback
                 if (client->app_callbacks.on_call_ended) {
                     client->app_callbacks.on_call_ended(client->current_call_id); // Pass relevant ID
                 }

             } else {
                 ESP_LOGW(TAG, "Received BYE but no active call. Sending 481 Call/Transaction Does Not Exist.");
                 send_sip_response(client, 481, "Call/Transaction Does Not Exist", NULL); // Need context
             }

        } else if (strcmp(method, "CANCEL") == 0) {
            // Request to cancel a pending INVITE (usually incoming)
             if (client->call_state == SIP_CALL_STATE_INCOMING || client->call_state == SIP_CALL_STATE_RINGING) {
                 ESP_LOGI(TAG, "Received CANCEL for pending INVITE.");
                 // Send 200 OK to the CANCEL
                 send_sip_response(client, 200, "OK", NULL); // Need CANCEL context
                 // Send 487 Request Terminated for the original INVITE
                 send_sip_response(client, 487, "Request Terminated", client->current_to_tag); // Need INVITE context + our tag
                 client->call_state = SIP_CALL_STATE_IDLE;
                 client->current_call_id[0] = '\0';
                 if (client->app_callbacks.on_call_ended) {
                     client->app_callbacks.on_call_ended(client->current_call_id); // Indicate call ended/cancelled
                 }
             } else {
                  ESP_LOGW(TAG, "Received CANCEL but no matching pending INVITE. Sending 481.");
                  send_sip_response(client, 481, "Call/Transaction Does Not Exist", NULL);
             }
        } else if (strcmp(method, "OPTIONS") == 0) {
             ESP_LOGI(TAG, "Received OPTIONS request. Sending 200 OK.");
             // Send 200 OK response, listing allowed methods in Allow header
             send_sip_response(client, 200, "OK", NULL); // Need context
        } else {
            ESP_LOGW(TAG, "Received unsupported SIP method: %s. Sending 501 Not Implemented.", method);
            send_sip_response(client, 501, "Not Implemented", NULL); // Need context
        }
    }
}
