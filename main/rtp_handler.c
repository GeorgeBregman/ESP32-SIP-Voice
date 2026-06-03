// rtp_handler.c
#include "rtp_handler.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>
#include <inttypes.h>

static const char* TAG = "RTP";

// Largest encoded payload we ever buffer (OPUS 20ms worst case is well under
// this; G.711/G.722 are 160 bytes). Keeps the jitter entries fixed-size.
#define RTP_JB_MAX_PAYLOAD 512

typedef struct {
    bool     is_valid;
    uint16_t seq;
    uint32_t ts;
    uint8_t  payload[RTP_JB_MAX_PAYLOAD];
    size_t   payload_len;
} rtp_packet_entry_t;

typedef struct rtp_session_s {
    int rtp_socket;
    uint16_t local_port;
    uint32_t ssrc;
    uint16_t sequence_number;
    uint32_t timestamp_offset;     // Random initial timestamp offset
    // --- Jitter Buffer Fields ---
    rtp_packet_entry_t *jitter_buffer;
    size_t jitter_buffer_size;
    SemaphoreHandle_t jitter_mutex;
    uint16_t next_playout_seq;
    bool is_buffering;
    int valid_packets_count;
} rtp_session_t;

rtp_session_handle_t rtp_session_create(uint16_t local_port) {
    rtp_session_t* session = (rtp_session_t*)calloc(1, sizeof(rtp_session_t));
    if (!session) {
        ESP_LOGE(TAG, "Failed to allocate RTP session memory");
        return NULL;
    }

    session->local_port = local_port;
    session->ssrc = esp_random();
    session->sequence_number = (uint16_t)(esp_random() & 0xFFFF);
    session->timestamp_offset = esp_random();

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

    // CRITICAL: make the receive side non-blocking so the real-time audio task
    // never stalls waiting for an RTP packet. We drain whatever is available
    // each 20 ms cycle.
    int flags = fcntl(session->rtp_socket, F_GETFL, 0);
    fcntl(session->rtp_socket, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "RTP session created. SSRC: 0x%08" PRIX32 ", listening on port %d",
             session->ssrc, session->local_port);

    // Allocate the jitter buffer. Prefer a large PSRAM buffer when available
    // (PRO tier), otherwise fall back to a small internal-SRAM buffer.
#if defined(CONFIG_SPIRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    session->jitter_buffer_size = 50;
    session->jitter_buffer = (rtp_packet_entry_t *)heap_caps_calloc(
        session->jitter_buffer_size, sizeof(rtp_packet_entry_t), MALLOC_CAP_SPIRAM);
    if (session->jitter_buffer) {
        ESP_LOGI(TAG, "Allocated %d-packet jitter buffer in PSRAM", (int)session->jitter_buffer_size);
    }
#else
    session->jitter_buffer = NULL;
#endif

    if (!session->jitter_buffer) {
        session->jitter_buffer_size = JITTER_BUFFER_SIZE;
        session->jitter_buffer = (rtp_packet_entry_t *)calloc(session->jitter_buffer_size, sizeof(rtp_packet_entry_t));
        if (!session->jitter_buffer) {
            ESP_LOGE(TAG, "Failed to allocate memory for Jitter Buffer");
            close(session->rtp_socket);
            free(session);
            return NULL;
        }
    }

    session->jitter_mutex = xSemaphoreCreateMutex();
    session->is_buffering = true;
    session->valid_packets_count = 0;
    return session;
}

void rtp_session_delete(rtp_session_handle_t handle) {
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session) return;

    if (session->rtp_socket >= 0) {
        close(session->rtp_socket);
    }
    if (session->jitter_mutex) {
        vSemaphoreDelete(session->jitter_mutex);
    }
    if (session->jitter_buffer) {
        free(session->jitter_buffer);
    }
    free(session);
    ESP_LOGI(TAG, "RTP session deleted.");
}

esp_err_t rtp_send_packet(rtp_session_handle_t handle, const ip_addr_t* remote_ip, uint16_t remote_port,
                          uint8_t payload_type, uint32_t current_timestamp, uint8_t marker,
                          const uint8_t* payload, size_t payload_len)
{
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session || !remote_ip || remote_port == 0 || !payload || payload_len == 0 || session->rtp_socket < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rtp_buffer[sizeof(rtp_header_t) + RTP_JB_MAX_PAYLOAD];
    if (sizeof(rtp_header_t) + payload_len > sizeof(rtp_buffer)) {
        ESP_LOGE(TAG, "Payload too large for RTP buffer (%d)", (int)payload_len);
        return ESP_ERR_INVALID_SIZE;
    }

    rtp_header_t* header = (rtp_header_t*)rtp_buffer;
    header->version = 2;
    header->p = 0;
    header->x = 0;
    header->cc = 0;
    header->m = marker ? 1 : 0;
    header->pt = payload_type & 0x7F;
    header->seq = htons(session->sequence_number++);
    header->ts = htonl(session->timestamp_offset + current_timestamp);
    header->ssrc = htonl(session->ssrc);

    memcpy(rtp_buffer + sizeof(rtp_header_t), payload, payload_len);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(remote_port);
    memcpy(&dest_addr.sin_addr, remote_ip, sizeof(struct in_addr));

    int sent_len = sendto(session->rtp_socket, rtp_buffer, sizeof(rtp_header_t) + payload_len, 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent_len < 0) {
        ESP_LOGW(TAG, "sendto RTP failed: errno %d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

int rtp_receive_packet(rtp_session_handle_t handle, uint8_t* buffer, size_t buffer_len,
                       ip_addr_t* remote_ip, uint16_t* remote_port, rtp_header_t* out_header)
{
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session || !buffer || buffer_len < sizeof(rtp_header_t) || !remote_ip || !remote_port || !out_header
        || session->rtp_socket < 0) {
        return -1;
    }

    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    int received_len = recvfrom(session->rtp_socket, buffer, buffer_len, 0,
                                (struct sockaddr*)&src_addr, &addr_len);

    if (received_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // No data available (non-blocking socket)
        }
        ESP_LOGW(TAG, "recvfrom RTP failed: errno %d", errno);
        return -1;
    }

    if (received_len < (int)sizeof(rtp_header_t)) {
        ESP_LOGW(TAG, "Received runt RTP packet (%d bytes)", received_len);
        return -1;
    }

    memcpy(out_header, buffer, sizeof(rtp_header_t));
    out_header->seq = ntohs(out_header->seq);
    out_header->ts = ntohl(out_header->ts);
    out_header->ssrc = ntohl(out_header->ssrc);

    if (out_header->version != 2) {
        ESP_LOGW(TAG, "Invalid RTP version: %d", out_header->version);
        return -1;
    }

    memcpy(remote_ip, &src_addr.sin_addr, sizeof(struct in_addr));
    *remote_port = ntohs(src_addr.sin_port);

    int payload_len = received_len - (int)sizeof(rtp_header_t);
    return payload_len;
}

uint32_t rtp_get_ssrc(rtp_session_handle_t handle) {
    rtp_session_t* session = (rtp_session_t*)handle;
    return session ? session->ssrc : 0;
}

// --- Jitter Buffer Implementation ---

void rtp_jitter_buffer_put(rtp_session_handle_t handle, const rtp_header_t* header, const uint8_t* payload, size_t len) {
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session || !session->jitter_mutex || !header || !payload) return;

    xSemaphoreTake(session->jitter_mutex, portMAX_DELAY);

    if (session->is_buffering && session->valid_packets_count == 0) {
        session->next_playout_seq = header->seq;
    }

    int idx = header->seq % session->jitter_buffer_size;

    if (!session->jitter_buffer[idx].is_valid || session->jitter_buffer[idx].seq != header->seq) {
        session->jitter_buffer[idx].is_valid = true;
        session->jitter_buffer[idx].seq = header->seq;
        session->jitter_buffer[idx].ts = header->ts;
        size_t copy_len = len < RTP_JB_MAX_PAYLOAD ? len : RTP_JB_MAX_PAYLOAD;
        memcpy(session->jitter_buffer[idx].payload, payload, copy_len);
        session->jitter_buffer[idx].payload_len = copy_len;
        session->valid_packets_count++;
    }

    if (session->is_buffering && session->valid_packets_count >= (int)(session->jitter_buffer_size / 2)) {
        session->is_buffering = false;
    }

    xSemaphoreGive(session->jitter_mutex);
}

// Returns the number of encoded payload bytes copied into payload_buffer, or 0
// when the buffer is pre-rolling / the next packet is missing (caller should
// emit a Packet-Loss-Concealment / silence frame for the active codec).
int rtp_jitter_buffer_get(rtp_session_handle_t handle, rtp_header_t* header, uint8_t* payload_buffer, size_t buffer_len) {
    rtp_session_t* session = (rtp_session_t*)handle;
    if (!session || !session->jitter_mutex || !payload_buffer) return 0;

    xSemaphoreTake(session->jitter_mutex, portMAX_DELAY);

    if (session->is_buffering) {
        xSemaphoreGive(session->jitter_mutex);
        return 0; // still pre-rolling -> PLC/silence
    }

    int idx = session->next_playout_seq % session->jitter_buffer_size;
    int return_len = 0;

    if (session->jitter_buffer[idx].is_valid && session->jitter_buffer[idx].seq == session->next_playout_seq) {
        return_len = (int)session->jitter_buffer[idx].payload_len;
        size_t copy_len = (size_t)return_len < buffer_len ? (size_t)return_len : buffer_len;
        memcpy(payload_buffer, session->jitter_buffer[idx].payload, copy_len);

        if (header) {
            header->seq = session->jitter_buffer[idx].seq;
            header->ts = session->jitter_buffer[idx].ts;
        }

        session->jitter_buffer[idx].is_valid = false;
        session->valid_packets_count--;
        session->next_playout_seq++;
        return_len = (int)copy_len;
    } else {
        // Packet lost or late -> PLC.
        if (session->valid_packets_count == 0) {
            session->is_buffering = true; // re-prime when fully drained
        } else {
            session->next_playout_seq++;  // skip the missing one
        }
        return_len = 0;
    }

    xSemaphoreGive(session->jitter_mutex);
    return return_len;
}
