// rtp_handler.h
#ifndef RTP_HANDLER_H
#define RTP_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include "lwip/ip_addr.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtp_session_s* rtp_session_handle_t;

// RTP fixed header (RFC 3550). Bit-field layout assumes a little-endian
// target (ESP32 / Xtensa / RISC-V are all little-endian), which makes the
// in-memory order match the on-the-wire byte 0/1 layout.
typedef struct {
    uint8_t cc : 4;       /* CSRC count */
    uint8_t x : 1;        /* header extension flag */
    uint8_t p : 1;        /* padding flag */
    uint8_t version : 2;  /* protocol version (2) */
    uint8_t pt : 7;       /* payload type */
    uint8_t m : 1;        /* marker bit */
    uint16_t seq;         /* sequence number */
    uint32_t ts;          /* timestamp */
    uint32_t ssrc;        /* synchronization source */
} __attribute__((packed)) rtp_header_t;

rtp_session_handle_t rtp_session_create(uint16_t local_port);
void rtp_session_delete(rtp_session_handle_t handle);

esp_err_t rtp_send_packet(rtp_session_handle_t handle, const ip_addr_t* remote_ip, uint16_t remote_port,
                          uint8_t payload_type, uint32_t timestamp, uint8_t marker,
                          const uint8_t* payload, size_t payload_len);

int rtp_receive_packet(rtp_session_handle_t handle, uint8_t* buffer, size_t buffer_len,
                       ip_addr_t* remote_ip, uint16_t* remote_port, rtp_header_t* out_header);

uint32_t rtp_get_ssrc(rtp_session_handle_t handle);

// Jitter buffer
void rtp_jitter_buffer_put(rtp_session_handle_t handle, const rtp_header_t* header, const uint8_t* payload, size_t len);
int  rtp_jitter_buffer_get(rtp_session_handle_t handle, rtp_header_t* header, uint8_t* payload_buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif // RTP_HANDLER_H
