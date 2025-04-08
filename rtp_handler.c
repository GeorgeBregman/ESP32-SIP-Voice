// rtp_handler.h
#ifndef RTP_HANDLER_H
#define RTP_HANDLER_H

#include <stdint.h>
#include "lwip/ip_addr.h"
#include "esp_err.h"

typedef struct rtp_session_s* rtp_session_handle_t;

// Structure for RTP header
typedef struct {
    uint8_t cc : 4;       /* CSRC count */
    uint8_t x : 1;        /* header extension flag */
    uint8_t p : 1;        /* padding flag */
    uint8_t version : 2;  /* protocol version */
    uint8_t pt : 7;       /* payload type */
    uint8_t m : 1;        /* marker bit */
    uint16_t seq;         /* sequence number */
    uint32_t ts;          /* timestamp */
    uint32_t ssrc;        /* synchronization source */
    // uint32_t csrc[1];    /* optional CSRC list */
} __attribute__((packed)) rtp_header_t;


rtp_session_handle_t rtp_session_create(uint16_t local_port);
void rtp_session_delete(rtp_session_handle_t handle);
esp_err_t rtp_send_packet(rtp_session_handle_t handle, const ip_addr_t* remote_ip, uint16_t remote_port,
                          uint8_t payload_type, uint32_t timestamp, const uint8_t* payload, size_t payload_len);
int rtp_receive_packet(rtp_session_handle_t handle, uint8_t* buffer, size_t buffer_len,
                       ip_addr_t* remote_ip, uint16_t* remote_port, rtp_header_t* out_header);
uint32_t rtp_get_ssrc(rtp_session_handle_t handle);

// Jitter buffer functions (placeholders)
void rtp_jitter_buffer_put(rtp_session_handle_t handle, const rtp_header_t* header, const uint8_t* payload, size_t len);
int rtp_jitter_buffer_get(rtp_session_handle_t handle, rtp_header_t* header, uint8_t* payload_buffer, size_t buffer_len);

#endif // RTP_HANDLER_H

// rtp_handler.c
#include "rtp_handler.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include <string.h>

static const char* TAG = "RTP";

typedef struct rtp_session_s {
    int rtp_socket;
    uint16_t local_port;
    uint32_t ssrc;
    uint16_t sequence_number;
    uint32_t timestamp_offset; // Initial timestamp based on sample rate
    // --- Jitter Buffer Fields ---
    // Example: simple circular buffer
    // rtp_packet_entry_t jitter_buffer[JITTER_BUFFER_SIZE];
    // int jitter_read_idx;
    // int jitter_write_idx;
    // int jitter_count;
    // uint16_t last_played_seq;
    // Add mutex for jitter buffer access
} rtp_session_t;

rtp_session_handle_t rtp_session_create(uint16_t local_port) {
    rtp_session_t* session = (rtp_session_t*)calloc(1, sizeof(rtp_session_t));
    if (!session) {
        ESP_LOGE(TAG, "Failed to allocate RTP session memory");
        return NULL;
    }

    session->local_port = local_port;
    session->ssrc = esp_random(); // Generate random SSRC
    session->sequence_number = (uint16_t)(esp_random() & 0xFFFF); // Random initial sequence
    session->timestamp_offset = esp_random(); // Random initial timestamp offset

    session->rtp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (session->rtp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create RTP socket: errno %d", errno);
        free(session);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->local_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(session->rtp_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "RTP Socket bind failed for port %d: errno %d", session->local_port, errno);
        close(session->rtp_socket);
        free(session);
        return NULL;
    }

    ESP_LOGI(TAG, "RTP session created. SSRC: 0x%08lX, Listening on port %d", session->ssrc, session->local_port);
    // Initialize jitter buffer here
    return session;
}

void rtp_session_delete(rtp_session_handle_t handle) {
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session) return;

    if (session->rtp_socket >= 0) {
        close(session->rtp_socket);
    }
    // Delete jitter buffer resources
    free(session);
    ESP_LOGI(TAG, "RTP session deleted.");
}

esp_err_t rtp_send_packet(rtp_session_handle_t handle, const ip_addr_t* remote_ip, uint16_t remote_port,
                          uint8_t payload_type, uint32_t current_timestamp, const uint8_t* payload, size_t payload_len)
{
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session || !remote_ip || remote_port == 0 || !payload || payload_len == 0 || session->rtp_socket < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rtp_buffer[RTP_TX_BUFFER_SIZE]; // Use config size
    if (sizeof(rtp_header_t) + payload_len > sizeof(rtp_buffer)) {
        ESP_LOGE(TAG, "Payload size too large for RTP buffer (%d > %d)", (int)(sizeof(rtp_header_t) + payload_len), (int)sizeof(rtp_buffer));
        return ESP_ERR_INVALID_SIZE;
    }

    rtp_header_t* header = (rtp_header_t*)rtp_buffer;

    header->version = 2;
    header->p = 0;
    header->x = 0;
    header->cc = 0;
    header->m = 0; // Marker bit logic might be needed (e.g., end of talk spurt) - set to 0 for now
    header->pt = payload_type & 0x7F;
    header->seq = htons(session->sequence_number++); // Increment sequence number
    header->ts = htonl(session->timestamp_offset + current_timestamp); // Add offset to timestamp
    header->ssrc = htonl(session->ssrc);

    // Copy payload after the header
    memcpy(rtp_buffer + sizeof(rtp_header_t), payload, payload_len);

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(remote_port);
    memcpy(&dest_addr.sin_addr, remote_ip, sizeof(struct in_addr)); // Use ip_addr_t directly

    int sent_len = sendto(session->rtp_socket, rtp_buffer, sizeof(rtp_header_t) + payload_len, 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent_len < 0) {
        ESP_LOGE(TAG, "sendto RTP failed: errno %d", errno);
        return ESP_FAIL;
    } else if (sent_len != sizeof(rtp_header_t) + payload_len) {
        ESP_LOGW(TAG, "sendto RTP sent partial packet (%d / %d)", sent_len, (int)(sizeof(rtp_header_t) + payload_len));
        return ESP_FAIL; // Or maybe ESP_OK depending on tolerance
    }

    // ESP_LOGD(TAG, "Sent RTP Seq: %d, TS: %lu, Len: %d", ntohs(header->seq), ntohl(header->ts), sent_len);
    return ESP_OK;
}


int rtp_receive_packet(rtp_session_handle_t handle, uint8_t* buffer, size_t buffer_len,
                       ip_addr_t* remote_ip, uint16_t* remote_port, rtp_header_t* out_header)
{
     rtp_session_t* session = (rtp_session_t*)handle;
     if (!session || !buffer || buffer_len < sizeof(rtp_header_t) || !remote_ip || !remote_port || !out_header || session->rtp_socket < 0) {
         return -1; // Error indicator
     }

     struct sockaddr_in src_addr;
     socklen_t addr_len = sizeof(src_addr);

     // Use select or non-blocking recvfrom if integrating into a task loop
     // This is a blocking example for simplicity
     int received_len = recvfrom(session->rtp_socket, buffer, buffer_len, 0,
                                (struct sockaddr*)&src_addr, &addr_len);

     if (received_len < 0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) {
             return 0; // No data available (if non-blocking)
         }
         ESP_LOGE(TAG, "recvfrom RTP failed: errno %d", errno);
         return -1; // Error
     }

     if (received_len < sizeof(rtp_header_t)) {
         ESP_LOGW(TAG, "Received runt RTP packet (%d bytes)", received_len);
         return -1; // Error - too small
     }

     // Copy header for parsing (and potentially for jitter buffer)
     memcpy(out_header, buffer, sizeof(rtp_header_t));

     // Convert fields from network byte order
     out_header->seq = ntohs(out_header->seq);
     out_header->ts = ntohl(out_header->ts);
     out_header->ssrc = ntohl(out_header->ssrc);

     // Basic validation
     if (out_header->version != 2) {
          ESP_LOGW(TAG, "Received packet with invalid RTP version: %d", out_header->version);
          return -1;
     }
     // TODO: Add SSRC validation if needed (check against expected peer SSRC)

     // Populate sender info
     memcpy(remote_ip, &src_addr.sin_addr, sizeof(struct in_addr));
     *remote_port = ntohs(src_addr.sin_port);

     int payload_len = received_len - sizeof(rtp_header_t);
     // Note: The buffer passed in now contains the full packet (header + payload)
     // The caller needs to offset by sizeof(rtp_header_t) to get the payload.

     ESP_LOGD(TAG, "Recv RTP Seq: %d, TS: %lu, SSRC: 0x%08lX, PL Len: %d from %s:%d",
              out_header->seq, out_header->ts, out_header->ssrc, payload_len,
              ipaddr_ntoa(remote_ip), *remote_port);

     return payload_len; // Return payload length, or received_len? Payload len seems more useful.
}

uint32_t rtp_get_ssrc(rtp_session_handle_t handle) {
    rtp_session_t* session = (rtp_session_t*)handle;
    return session ? session->ssrc : 0;
}


// --- Jitter Buffer Implementation (Conceptual Placeholders) ---

void rtp_jitter_buffer_put(rtp_session_handle_t handle, const rtp_header_t* header, const uint8_t* payload, size_t len) {
     // TODO: Implement jitter buffer logic
     // - Acquire mutex
     // - Check if buffer is full
     // - Find correct insertion point based on sequence number (handle wrap-around)
     // - Store header info (seq, ts) and payload
     // - Update write index, count
     // - Release mutex
     ESP_LOGD(TAG, "Jitter Put: Seq=%d, Len=%d (Not Implemented)", header->seq, len);
}

int rtp_jitter_buffer_get(rtp_session_handle_t handle, rtp_header_t* header, uint8_t* payload_buffer, size_t buffer_len) {
     // TODO: Implement jitter buffer logic
     // - Acquire mutex
     // - Determine target playback time / target sequence number
     // - Check if the target packet (or next available) is present
     // - Handle packet loss (return silence/comfort noise flag, or generate PLC data)
     // - Handle late packets (discard?)
     // - If packet available:
     //    - Copy header and payload to output buffers
     //    - Update read index, count, last_played_seq
     //    - Release mutex
     //    - Return payload length
     // - Else (packet missing/not ready):
     //    - Release mutex
     //    - Return 0 or negative value indicating loss/wait
     ESP_LOGD(TAG, "Jitter Get (Not Implemented) - Playing silence/default");
     // Simulate silence for now
     memset(payload_buffer, 0, AUDIO_SAMPLES_PER_FRAME); // Assuming G711 1 byte per sample
     header->seq++; // Just increment seq artificially
     header->ts += AUDIO_SAMPLES_PER_FRAME; // Increment timestamp
     return AUDIO_SAMPLES_PER_FRAME; // Return expected length for silence
}
