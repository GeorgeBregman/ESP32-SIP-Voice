// sip_client.c
//
// UDP SIP user agent: REGISTER (MD5 Digest, RFC 2069 *and* RFC 2617 qop=auth),
// INVITE/ACK/BYE/CANCEL/INFO, SDP offer/answer with dynamic codec negotiation
// (OPUS / G.722 / G.711), STUN for NAT, and NAT keep-alive.
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "sip_client.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "freertos/timers.h"
#include "wifi_manager.h"        // get_my_ip()
#include "mbedtls/md5.h"
#include <inttypes.h>

static const char *TAG = "SIP_CLIENT";

// lwip's ip_addr_get_ip4_u32 / ip_addr_set_ip4_u32 macros null-check their
// pointer argument; passing the address of a local (`&x`) makes GCC 12 warn
// "address will always evaluate as true" (-Werror=address). Wrapping them so
// the macro sees a pointer *parameter* (not a constant address) avoids it.
static uint32_t ipaddr_to_u32(ip_addr_t *a) { return ip_addr_get_ip4_u32(a); }
static void     u32_to_ipaddr(ip_addr_t *dst, uint32_t v) { ip_addr_set_ip4_u32(dst, v); }

typedef struct sip_client_s {
    EventGroupHandle_t event_group;
    EventBits_t registered_bit;
    audio_pipeline_handle_t audio;
    sip_callbacks_t app_callbacks;
    TimerHandle_t registration_timer;
    TaskHandle_t sip_task_handle;
    app_settings_t *settings;

    int sip_socket;
    struct sockaddr_in server_addr;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_rtp_addr;
    struct sockaddr_in last_remote_addr;   // source of the last received request

    char local_ip_str[16];
    char public_ip_str[16];
    uint16_t local_sip_port;
    uint16_t local_rtp_port;
    uint16_t public_sip_port;
    bool stun_ok;

    char user[64];
    char password[64];
    char domain[128];
    char display_name[64];

    volatile bool is_registered;
    volatile sip_call_state_t call_state;
    TickType_t invite_start_ts;
    uint32_t current_cseq;
    char current_call_id[128];
    char current_from_tag[32];
    char current_to_tag[64];
    char current_remote_uri[128];
    char reg_call_id[64];

    int negotiated_pt;                     // chosen RTP payload type, -1 = none

    // Digest auth
    char auth_realm[64];
    char auth_nonce[128];
    char auth_qop[16];                     // "auth" or empty
    char auth_opaque[64];
    char auth_cnonce[24];
    bool has_auth_info;
    bool auth_is_proxy;                    // 407 vs 401

    // Stored headers of the last received request (for building responses)
    char last_via[256];
    char last_from[256];
    char last_to[256];
    char last_call_id[128];
    char last_cseq[64];
} sip_client_t;

// --- Forward declarations ---
static void sip_task(void *pvParameters);
static esp_err_t create_sip_socket(sip_client_t *client);
static void send_register(sip_client_t *client, bool initial_registration);
static void send_invite(sip_client_t *client, const char *target_uri);
static void send_ack(sip_client_t *client, const struct sockaddr_in *dst);
static void send_bye(sip_client_t *client);
static void send_sip_response(sip_client_t *client, int status_code, const char *reason_phrase, const char* to_tag);
static void process_incoming_sip(sip_client_t *client, char *buffer, int len, struct sockaddr_in *remote_addr);
static void registration_timer_callback(TimerHandle_t xTimer);
static bool parse_sdp(sip_client_t *client, const char *sdp_body, ip_addr_t *remote_ip, uint16_t *remote_port, int *chosen_pt);
static int generate_sdp(sip_client_t *client, char *buffer, size_t buffer_len, int answer_pt);

// ===================== Digest authentication =====================

static void md5_hex(const char *s, char out[33]) {
    unsigned char digest[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, (const unsigned char*)s, strlen(s));
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);
    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", digest[i]);
}

// Compute the Digest "response" value. Handles both legacy RFC 2069 and
// RFC 2617 qop=auth (with cnonce + nc).
static void compute_digest_response(sip_client_t *c, const char *method, const char *uri,
                                    const char *nc, char response[33]) {
    char buf[512];
    char ha1[33], ha2[33];

    snprintf(buf, sizeof(buf), "%s:%s:%s", c->user, c->auth_realm, c->password);
    md5_hex(buf, ha1);

    snprintf(buf, sizeof(buf), "%s:%s", method, uri);
    md5_hex(buf, ha2);

    if (c->auth_qop[0]) {
        snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s",
                 ha1, c->auth_nonce, nc, c->auth_cnonce, c->auth_qop, ha2);
    } else {
        snprintf(buf, sizeof(buf), "%s:%s:%s", ha1, c->auth_nonce, ha2);
    }
    md5_hex(buf, response);
}

// Build the Authorization / Proxy-Authorization header line (including CRLF).
static void build_auth_header(sip_client_t *c, const char *method, const char *uri,
                              char *out, size_t out_len) {
    const char *nc = "00000001";
    char response[33];
    compute_digest_response(c, method, uri, nc, response);

    const char *hdr_name = c->auth_is_proxy ? "Proxy-Authorization" : "Authorization";

    int n = snprintf(out, out_len,
        "%s: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5",
        hdr_name, c->user, c->auth_realm, c->auth_nonce, uri, response);

    if (c->auth_qop[0] && n > 0 && (size_t)n < out_len) {
        n += snprintf(out + n, out_len - n, ", qop=%s, nc=%s, cnonce=\"%s\"",
                      c->auth_qop, nc, c->auth_cnonce);
    }
    if (c->auth_opaque[0] && n > 0 && (size_t)n < out_len) {
        n += snprintf(out + n, out_len - n, ", opaque=\"%s\"", c->auth_opaque);
    }
    if (n > 0 && (size_t)n < out_len) {
        snprintf(out + n, out_len - n, "\r\n");
    }
}

// Parse realm/nonce/qop/opaque out of a WWW-/Proxy-Authenticate header value.
static void parse_auth_challenge(sip_client_t *c, const char *hdr) {
    char *p;
    c->auth_qop[0] = '\0';
    c->auth_opaque[0] = '\0';

    if ((p = strstr(hdr, "realm=\"")))  sscanf(p + 7, "%63[^\"]", c->auth_realm);
    if ((p = strstr(hdr, "nonce=\"")))  sscanf(p + 7, "%127[^\"]", c->auth_nonce);
    if ((p = strstr(hdr, "opaque=\""))) sscanf(p + 8, "%63[^\"]", c->auth_opaque);
    if ((p = strstr(hdr, "qop=\""))) {
        char qop_list[64] = {0};
        sscanf(p + 5, "%63[^\"]", qop_list);
        // Prefer "auth" if offered.
        if (strstr(qop_list, "auth")) strcpy(c->auth_qop, "auth");
    }
    // Fresh client nonce per challenge.
    snprintf(c->auth_cnonce, sizeof(c->auth_cnonce), "%08" PRIx32 "%08" PRIx32,
             esp_random(), esp_random());
}

// ===================== Signaling address helpers =====================

static const char* sig_ip(sip_client_t *c)   { return c->stun_ok ? c->public_ip_str : c->local_ip_str; }
static uint16_t    sig_port(sip_client_t *c)  { return c->stun_ok ? c->public_sip_port : c->local_sip_port; }

// ===================== Codec negotiation helpers =====================

static bool pt_supported(int pt) {
    switch (pt) {
        case 0: case 8: return true;          // PCMU / PCMA always
#ifdef USE_CODEC_G722
        case 9: return true;
#endif
#ifdef USE_CODEC_OPUS
        case 96: return true;
#endif
        default: return false;
    }
}

// Our preference order (best first), restricted to compiled-in codecs.
static int pt_rank(int pt) {
    switch (pt) {
        case 96: return 4;   // OPUS
        case 9:  return 3;   // G722
        case 8:  return 2;   // PCMA
        case 0:  return 1;   // PCMU
        default: return 0;
    }
}

// ===================== Public API =====================

sip_client_handle_t sip_client_init(EventGroupHandle_t app_event_group, EventBits_t registered_event_bit,
                                    audio_pipeline_handle_t audio_handle, app_settings_t *settings) {
    ESP_LOGI(TAG, "Initializing SIP Client...");
    sip_client_t *client = (sip_client_t *)calloc(1, sizeof(sip_client_t));
    if (!client) {
        ESP_LOGE(TAG, "alloc failed");
        return NULL;
    }

    client->event_group   = app_event_group;
    client->registered_bit = registered_event_bit;
    client->audio         = audio_handle;
    client->settings      = settings;
    client->sip_socket    = -1;
    client->is_registered = false;
    client->call_state    = SIP_CALL_STATE_IDLE;
    client->current_cseq  = 1;
    client->local_sip_port = SIP_LOCAL_PORT;
    client->local_rtp_port = RTP_LOCAL_PORT_BASE;
    client->has_auth_info = false;
    client->negotiated_pt = -1;
    snprintf(client->reg_call_id, sizeof(client->reg_call_id), "%08" PRIx32 "%08" PRIx32,
             esp_random(), esp_random());

    strncpy(client->user,         settings->sip_user,     sizeof(client->user) - 1);
    strncpy(client->password,     settings->sip_password, sizeof(client->password) - 1);
    strncpy(client->domain,       settings->sip_server,   sizeof(client->domain) - 1);
    strncpy(client->display_name, SIP_DISPLAY_NAME,       sizeof(client->display_name) - 1);

    // Resolve server (DNS, fallback to literal IP).
    ip_addr_t target_addr;
    err_t err = dns_gethostbyname(settings->sip_server, &target_addr, NULL, NULL);
    if (err != ERR_OK) {
        if (!ipaddr_aton(settings->sip_server, &target_addr)) {
            ESP_LOGE(TAG, "Failed to resolve server '%s'", settings->sip_server);
            free(client);
            return NULL;
        }
    }
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port   = htons(SIP_SERVER_PORT);
    client->server_addr.sin_addr.s_addr = ipaddr_to_u32(&target_addr);

    client->registration_timer = xTimerCreate("RegTimer",
        pdMS_TO_TICKS((uint32_t)(SIP_REGISTRATION_EXPIRY * 1000 * 0.9)),
        pdFALSE, (void *)client, registration_timer_callback);
    if (!client->registration_timer) {
        ESP_LOGE(TAG, "timer create failed");
        free(client);
        return NULL;
    }

    if (xTaskCreate(sip_task, "sip_task", SIP_TASK_STACK_SIZE, client, SIP_TASK_PRIORITY,
                    &client->sip_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        xTimerDelete(client->registration_timer, portMAX_DELAY);
        free(client);
        return NULL;
    }

    ESP_LOGI(TAG, "SIP Client initialized.");
    return client;
}

esp_err_t sip_client_register_callbacks(sip_client_handle_t handle, const sip_callbacks_t* callbacks) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client || !callbacks) return ESP_ERR_INVALID_ARG;
    client->app_callbacks = *callbacks;
    return ESP_OK;
}

esp_err_t sip_client_start_registration(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Registration handled by SIP task.");
    return ESP_OK;
}

esp_err_t sip_client_initiate_call(sip_client_handle_t handle, const char* target_uri) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client || !target_uri || client->call_state != SIP_CALL_STATE_IDLE || !client->is_registered) {
        ESP_LOGE(TAG, "Cannot call: state=%d registered=%d", client ? client->call_state : -1,
                 client ? client->is_registered : 0);
        return ESP_FAIL;
    }
    strncpy(client->current_remote_uri, target_uri, sizeof(client->current_remote_uri) - 1);
    client->call_state = SIP_CALL_STATE_INVITING;
    client->invite_start_ts = xTaskGetTickCount();
    client->negotiated_pt = -1;
    snprintf(client->current_call_id, sizeof(client->current_call_id), "%08" PRIx32 "%08" PRIx32,
             esp_random(), esp_random());
    snprintf(client->current_from_tag, sizeof(client->current_from_tag), "%08" PRIx32, esp_random());
    client->current_to_tag[0] = '\0';

    ESP_LOGI(TAG, "Calling %s (Call-ID %s)", target_uri, client->current_call_id);
    send_invite(client, target_uri);
    return ESP_OK;
}

esp_err_t sip_client_answer_call(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client || client->call_state != SIP_CALL_STATE_INCOMING) {
        ESP_LOGE(TAG, "Cannot answer: state=%d", client ? client->call_state : -1);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Answering call %s", client->current_call_id);
    send_sip_response(client, 200, "OK", client->current_to_tag);
    client->call_state = SIP_CALL_STATE_CONNECTING; // wait for ACK before audio
    return ESP_OK;
}

esp_err_t sip_client_terminate_call(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client) return ESP_ERR_INVALID_ARG;

    if (client->call_state == SIP_CALL_STATE_ACTIVE ||
        client->call_state == SIP_CALL_STATE_CONNECTING ||
        client->call_state == SIP_CALL_STATE_RINGING ||
        client->call_state == SIP_CALL_STATE_INCOMING ||
        client->call_state == SIP_CALL_STATE_INVITING) {

        char ended_id[128];
        strncpy(ended_id, client->current_call_id, sizeof(ended_id) - 1);
        ended_id[sizeof(ended_id) - 1] = '\0';

        ESP_LOGI(TAG, "Terminating call %s (state %d)", client->current_call_id, client->call_state);
        send_bye(client);
        client->call_state = SIP_CALL_STATE_TERMINATING;

        if (client->audio) audio_pipeline_stop(client->audio);

        client->call_state = SIP_CALL_STATE_IDLE;
        client->current_call_id[0] = '\0';
        client->negotiated_pt = -1;
        if (client->app_callbacks.on_call_ended) client->app_callbacks.on_call_ended(ended_id);
    } else {
        ESP_LOGW(TAG, "No active call (state %d)", client->call_state);
        return ESP_FAIL;
    }
    return ESP_OK;
}

uint16_t sip_client_get_local_rtp_port(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    return client ? client->local_rtp_port : 0;
}

int sip_client_get_negotiated_pt(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    return client ? client->negotiated_pt : -1;
}

esp_err_t sip_client_get_remote_rtp_info(sip_client_handle_t handle, ip_addr_t* remote_ip, uint16_t* remote_port) {
    sip_client_t *client = (sip_client_t *)handle;
    if (!client || !remote_ip || !remote_port || client->call_state < SIP_CALL_STATE_CONNECTING) {
        return ESP_FAIL;
    }
    u32_to_ipaddr(remote_ip, client->remote_rtp_addr.sin_addr.s_addr);
    *remote_port = ntohs(client->remote_rtp_addr.sin_port);
    return ESP_OK;
}

sip_call_state_t sip_client_get_call_state(sip_client_handle_t handle) {
    sip_client_t *client = (sip_client_t *)handle;
    return client ? client->call_state : SIP_CALL_STATE_IDLE;
}

// ===================== STUN =====================

static void perform_stun_lookup(sip_client_t *client) {
#if USE_STUN
    ESP_LOGI(TAG, "STUN lookup %s:%d", STUN_SERVER_IP, STUN_SERVER_PORT);
    ip_addr_t stun_addr;
    if (dns_gethostbyname(STUN_SERVER_IP, &stun_addr, NULL, NULL) != ERR_OK) {
        if (!ipaddr_aton(STUN_SERVER_IP, &stun_addr)) {
            ESP_LOGE(TAG, "STUN resolve failed");
            return;
        }
    }
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(STUN_SERVER_PORT);
    dest.sin_addr.s_addr = ipaddr_to_u32(&stun_addr);

    uint8_t req[20] = { 0x00,0x01,0x00,0x00, 0x21,0x12,0xA4,0x42 };
    for (int i = 0; i < 12; i++) req[8 + i] = esp_random() & 0xFF;
    sendto(client->sip_socket, req, sizeof(req), 0, (struct sockaddr*)&dest, sizeof(dest));

    fd_set rf; FD_ZERO(&rf); FD_SET(client->sip_socket, &rf);
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    if (select(client->sip_socket + 1, &rf, NULL, NULL, &tv) > 0) {
        uint8_t resp[256];
        struct sockaddr_in src; socklen_t sl = sizeof(src);
        int len = recvfrom(client->sip_socket, resp, sizeof(resp), 0, (struct sockaddr*)&src, &sl);
        if (len >= 20 && resp[0] == 0x01 && resp[1] == 0x01) {
            uint16_t attr_len = (resp[2] << 8) | resp[3];
            int off = 20;
            while (off + 4 <= len && off - 20 < attr_len) {
                uint16_t a_type = (resp[off] << 8) | resp[off + 1];
                uint16_t a_len  = (resp[off + 2] << 8) | resp[off + 3];
                off += 4;
                if (a_type == 0x0020 && a_len >= 8) { // XOR-MAPPED-ADDRESS
                    uint16_t port = ((resp[off + 2] << 8) | resp[off + 3]) ^ 0x2112;
                    uint32_t ip = ((resp[off + 4] << 24) | (resp[off + 5] << 16) |
                                   (resp[off + 6] << 8) | resp[off + 7]) ^ 0x2112A442;
                    struct in_addr pub = { .s_addr = htonl(ip) };
                    snprintf(client->public_ip_str, sizeof(client->public_ip_str), "%s", inet_ntoa(pub));
                    client->public_sip_port = port;
                    client->stun_ok = true;
                    ESP_LOGI(TAG, "STUN OK: public %s:%d", client->public_ip_str, port);
                    break;
                }
                off += a_len;
            }
        }
    } else {
        ESP_LOGW(TAG, "STUN timed out");
    }
#endif
}

// ===================== SIP task =====================

static void sip_task(void *pvParameters) {
    sip_client_t *client = (sip_client_t *)pvParameters;
    char rx_buffer[2048];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "SIP task waiting for IP...");
    xEventGroupWaitBits(client->event_group, IP_ACQUIRED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    esp_ip4_addr_t my_ip;
    if (get_my_ip(&my_ip) != ESP_OK) {
        ESP_LOGE(TAG, "No local IP; SIP task exiting.");
        vTaskDelete(NULL);
        return;
    }
    snprintf(client->local_ip_str, sizeof(client->local_ip_str), IPSTR, IP2STR(&my_ip));
    ESP_LOGI(TAG, "Local IP: %s", client->local_ip_str);

    if (create_sip_socket(client) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    perform_stun_lookup(client);
    send_register(client, true);

    TickType_t last_keepalive = xTaskGetTickCount();

    while (1) {
        fd_set rf; FD_ZERO(&rf); FD_SET(client->sip_socket, &rf);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int s = select(client->sip_socket + 1, &rf, NULL, NULL, &tv);

        if (client->call_state == SIP_CALL_STATE_INVITING && client->invite_start_ts > 0) {
            if (pdTICKS_TO_MS(xTaskGetTickCount() - client->invite_start_ts) > 32000) {
                ESP_LOGE(TAG, "INVITE Timeout (Timer B) - No answer received");
                client->call_state = SIP_CALL_STATE_IDLE;
                if (client->app_callbacks.on_call_ended) {
                    client->app_callbacks.on_call_ended(client->current_call_id);
                }
                client->invite_start_ts = 0;
            }
        }

        if (s < 0) {
            ESP_LOGE(TAG, "select errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        } else if (s == 0) {
            if (pdTICKS_TO_MS(xTaskGetTickCount() - last_keepalive) > 20000) {
                last_keepalive = xTaskGetTickCount();
                if (client->is_registered) {
                    sendto(client->sip_socket, "\r\n\r\n", 4, 0,
                           (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
                }
            }
        } else if (FD_ISSET(client->sip_socket, &rf)) {
            int len = recvfrom(client->sip_socket, rx_buffer, sizeof(rx_buffer) - 1, 0,
                               (struct sockaddr *)&source_addr, &socklen);
            if (len > 0) {
                rx_buffer[len] = '\0';
                process_incoming_sip(client, rx_buffer, len, &source_addr);
            }
        }
    }
}

static esp_err_t create_sip_socket(sip_client_t *client) {
    client->sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client->sip_socket < 0) {
        ESP_LOGE(TAG, "socket errno %d", errno);
        return ESP_FAIL;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(client->local_sip_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(client->sip_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind errno %d", errno);
        close(client->sip_socket);
        client->sip_socket = -1;
        return ESP_FAIL;
    }
    socklen_t al = sizeof(client->local_addr);
    getsockname(client->sip_socket, (struct sockaddr*)&client->local_addr, &al);
    client->local_sip_port = ntohs(client->local_addr.sin_port);
    ESP_LOGI(TAG, "SIP socket bound to port %d", client->local_sip_port);
    return ESP_OK;
}

static void registration_timer_callback(TimerHandle_t xTimer) {
    sip_client_t *client = (sip_client_t *)pvTimerGetTimerID(xTimer);
    if (client && client->sip_socket >= 0) {
        send_register(client, false);
    }
}

// ===================== Message builders =====================

static void send_register(sip_client_t *client, bool initial_registration) {
    char buffer[1200];
    char branch[32];
    char auth_header[400] = {0};
    char uri[160];

    snprintf(branch, sizeof(branch), "z9hG4bK%08" PRIx32, esp_random());
    snprintf(uri, sizeof(uri), "sip:%s", client->domain);

    if (client->has_auth_info) {
        build_auth_header(client, "REGISTER", uri, auth_header, sizeof(auth_header));
    }

    int len = snprintf(buffer, sizeof(buffer),
        "REGISTER sip:%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: \"%s\" <sip:%s@%s>;tag=%s\r\n"
        "To: \"%s\" <sip:%s@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %" PRIu32 " REGISTER\r\n"
        "Contact: <sip:%s@%s:%d>\r\n"
        "%s"
        "Expires: %d\r\n"
        "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, INFO\r\n"
        "User-Agent: ESP32-SIPVoice/2.1\r\n"
        "Content-Length: 0\r\n\r\n",
        client->domain,
        sig_ip(client), sig_port(client), branch,
        client->display_name, client->user, client->domain, client->current_from_tag[0] ? client->current_from_tag : "reg",
        client->display_name, client->user, client->domain,
        client->reg_call_id,
        client->current_cseq++,
        client->user, sig_ip(client), sig_port(client),
        auth_header,
        initial_registration ? SIP_REGISTRATION_EXPIRY : SIP_REGISTRATION_EXPIRY);

    if (len > 0 && len < (int)sizeof(buffer)) {
        ESP_LOGI(TAG, "TX REGISTER (CSeq %" PRIu32 ")", client->current_cseq - 1);
        sendto(client->sip_socket, buffer, len, 0,
               (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
    } else {
        ESP_LOGE(TAG, "REGISTER buffer overflow");
    }
}

// Append an a=rtpmap line for a payload type.
static int append_rtpmap(char *buf, size_t len, int pt) {
    switch (pt) {
        case 96: return snprintf(buf, len, "a=rtpmap:96 opus/48000/2\r\na=fmtp:96 maxplaybackrate=48000;stereo=0\r\n");
        case 9:  return snprintf(buf, len, "a=rtpmap:9 G722/8000\r\n");
        case 8:  return snprintf(buf, len, "a=rtpmap:8 PCMA/8000\r\n");
        case 0:  return snprintf(buf, len, "a=rtpmap:0 PCMU/8000\r\n");
        default: return 0;
    }
}

// Build an SDP body. answer_pt < 0 => offer all compiled codecs.
// answer_pt >= 0 => answer with exactly that codec.
static int generate_sdp(sip_client_t *client, char *buffer, size_t buffer_len, int answer_pt) {
    char m_line[64];
    char rtpmaps[256] = {0};
    int rl = 0;

    if (answer_pt >= 0) {
        snprintf(m_line, sizeof(m_line), "%d", answer_pt);
        rl += append_rtpmap(rtpmaps + rl, sizeof(rtpmaps) - rl, answer_pt);
    } else {
        // Offer in preference order, compiled-in only.
        int offered = 0;
        m_line[0] = '\0';
#ifdef USE_CODEC_OPUS
        strcat(m_line, offered++ ? " 96" : "96"); rl += append_rtpmap(rtpmaps + rl, sizeof(rtpmaps) - rl, 96);
#endif
#ifdef USE_CODEC_G722
        strcat(m_line, offered++ ? " 9" : "9");   rl += append_rtpmap(rtpmaps + rl, sizeof(rtpmaps) - rl, 9);
#endif
        strcat(m_line, offered++ ? " 8" : "8");    rl += append_rtpmap(rtpmaps + rl, sizeof(rtpmaps) - rl, 8);
        strcat(m_line, offered++ ? " 0" : "0");    rl += append_rtpmap(rtpmaps + rl, sizeof(rtpmaps) - rl, 0);
    }

    return snprintf(buffer, buffer_len,
        "v=0\r\n"
        "o=%s %" PRIu32 " %" PRIu32 " IN IP4 %s\r\n"
        "s=ESP32 Call\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio %d RTP/AVP %s\r\n"
        "%s"
        "a=ptime:%d\r\n"
        "a=sendrecv\r\n",
        client->user, esp_random(), esp_random(), sig_ip(client),
        sig_ip(client),
        client->local_rtp_port, m_line,
        rtpmaps,
        AUDIO_FRAME_MS);
}

static void send_invite(sip_client_t *client, const char *target_uri) {
    char buffer[2048];
    char sdp_buffer[600];
    char branch[32];
    char auth_header[400] = {0};

    snprintf(branch, sizeof(branch), "z9hG4bK%08" PRIx32, esp_random());

    int sdp_len = generate_sdp(client, sdp_buffer, sizeof(sdp_buffer), -1);
    if (sdp_len <= 0) {
        ESP_LOGE(TAG, "SDP generation failed");
        client->call_state = SIP_CALL_STATE_IDLE;
        return;
    }

    if (client->has_auth_info) {
        build_auth_header(client, "INVITE", target_uri, auth_header, sizeof(auth_header));
    }

    int len = snprintf(buffer, sizeof(buffer),
        "INVITE %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: \"%s\" <sip:%s@%s>;tag=%s\r\n"
        "To: <%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %" PRIu32 " INVITE\r\n"
        "Contact: <sip:%s@%s:%d>\r\n"
        "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, INFO\r\n"
        "Content-Type: application/sdp\r\n"
        "%s"
        "User-Agent: ESP32-SIPVoice/2.1\r\n"
        "Content-Length: %d\r\n\r\n"
        "%s",
        target_uri,
        sig_ip(client), sig_port(client), branch,
        client->display_name, client->user, client->domain, client->current_from_tag,
        target_uri,
        client->current_call_id,
        client->current_cseq++,
        client->user, sig_ip(client), sig_port(client),
        auth_header,
        sdp_len,
        sdp_buffer);

    if (len > 0 && len < (int)sizeof(buffer)) {
        ESP_LOGI(TAG, "TX INVITE (CSeq %" PRIu32 ")", client->current_cseq - 1);
        sendto(client->sip_socket, buffer, len, 0,
               (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
    } else {
        ESP_LOGE(TAG, "INVITE buffer overflow");
        client->call_state = SIP_CALL_STATE_IDLE;
    }
}

static void send_ack(sip_client_t *client, const struct sockaddr_in *dst) {
    char buffer[1024];
    char branch[32];
    snprintf(branch, sizeof(branch), "z9hG4bK%08" PRIx32, esp_random());

    int len = snprintf(buffer, sizeof(buffer),
        "ACK %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: \"%s\" <sip:%s@%s>;tag=%s\r\n"
        "To: <%s>%s%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %" PRIu32 " ACK\r\n"
        "Content-Length: 0\r\n\r\n",
        client->current_remote_uri,
        sig_ip(client), sig_port(client), branch,
        client->display_name, client->user, client->domain, client->current_from_tag,
        client->current_remote_uri,
        client->current_to_tag[0] ? ";tag=" : "", client->current_to_tag,
        client->current_call_id,
        client->current_cseq - 1);

    if (len > 0 && len < (int)sizeof(buffer)) {
        ESP_LOGI(TAG, "TX ACK");
        sendto(client->sip_socket, buffer, len, 0,
               (struct sockaddr *)dst, sizeof(*dst));
    }
}

static void send_bye(sip_client_t *client) {
    if (client->call_state < SIP_CALL_STATE_CONNECTING) return;
    char buffer[1024];
    char branch[32];
    snprintf(branch, sizeof(branch), "z9hG4bK%08" PRIx32, esp_random());

    int len = snprintf(buffer, sizeof(buffer),
        "BYE %s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: \"%s\" <sip:%s@%s>;tag=%s\r\n"
        "To: <%s>%s%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %" PRIu32 " BYE\r\n"
        "User-Agent: ESP32-SIPVoice/2.1\r\n"
        "Content-Length: 0\r\n\r\n",
        client->current_remote_uri,
        sig_ip(client), sig_port(client), branch,
        client->display_name, client->user, client->domain, client->current_from_tag,
        client->current_remote_uri,
        client->current_to_tag[0] ? ";tag=" : "", client->current_to_tag,
        client->current_call_id,
        client->current_cseq++);

    if (len > 0 && len < (int)sizeof(buffer)) {
        ESP_LOGI(TAG, "TX BYE (CSeq %" PRIu32 ")", client->current_cseq - 1);
        sendto(client->sip_socket, buffer, len, 0,
               (struct sockaddr *)&client->server_addr, sizeof(client->server_addr));
    }
}

static void send_sip_response(sip_client_t *client, int status_code, const char *reason_phrase, const char* to_tag) {
    char buffer[1200];
    char sdp_buffer[600];
    int sdp_len = 0;

    // Only a 200 OK that answers an incoming INVITE carries SDP. (A 200 OK to
    // BYE/CANCEL/OPTIONS/INFO must NOT include a session description.)
    if (status_code == 200 && client->call_state == SIP_CALL_STATE_INCOMING && client->negotiated_pt >= 0) {
        sdp_len = generate_sdp(client, sdp_buffer, sizeof(sdp_buffer), client->negotiated_pt);
    }

    // Only append our To-tag if the received To header does not already carry one.
    bool to_has_tag = (strstr(client->last_to, "tag=") != NULL);
    char to_tag_part[80] = {0};
    if (!to_has_tag && to_tag && to_tag[0]) {
        snprintf(to_tag_part, sizeof(to_tag_part), ";tag=%s", to_tag);
    }

    int len = snprintf(buffer, sizeof(buffer),
        "SIP/2.0 %d %s\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %s\r\n"
        "Contact: <sip:%s@%s:%d>\r\n"
        "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, INFO\r\n"
        "User-Agent: ESP32-SIPVoice/2.1\r\n"
        "%s"
        "Content-Length: %d\r\n\r\n"
        "%s",
        status_code, reason_phrase,
        client->last_via,
        client->last_from,
        client->last_to, to_tag_part,
        client->last_call_id,
        client->last_cseq,
        client->user, sig_ip(client), sig_port(client),
        (sdp_len > 0) ? "Content-Type: application/sdp\r\n" : "",
        sdp_len,
        (sdp_len > 0) ? sdp_buffer : "");

    if (len > 0 && len < (int)sizeof(buffer)) {
        ESP_LOGI(TAG, "TX %d %s", status_code, reason_phrase);
        sendto(client->sip_socket, buffer, len, 0,
               (struct sockaddr *)&client->last_remote_addr, sizeof(client->last_remote_addr));
    } else {
        ESP_LOGE(TAG, "Response buffer overflow");
    }
}

// ===================== Parsing =====================

// Portable case-insensitive substring search (avoids relying on the GNU
// strcasestr extension being present in every newlib configuration).
static const char* ci_strstr(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0) return hay;
    for (; *hay; hay++) {
        if (strncasecmp(hay, needle, nl) == 0) return hay;
    }
    return NULL;
}

static bool parse_header(const char *msg, const char *hdr_name, char *out_buf, size_t out_len) {
    const char *hdr_start = ci_strstr(msg, hdr_name);
    if (!hdr_start) return false;
    const char *value_start = hdr_start + strlen(hdr_name);
    while (*value_start == ' ' || *value_start == ':') value_start++;
    const char *value_end = strstr(value_start, "\r\n");
    if (!value_end) return false;
    size_t value_len = value_end - value_start;
    if (value_len >= out_len) value_len = out_len - 1;
    strncpy(out_buf, value_start, value_len);
    out_buf[value_len] = '\0';
    return true;
}

// Parse c= IP and the m=audio port + best supported payload type.
static bool parse_sdp(sip_client_t *client, const char *sdp_body, ip_addr_t *remote_ip,
                      uint16_t *remote_port, int *chosen_pt) {
    if (!sdp_body) return false;

    const char *c_line = strstr(sdp_body, "c=");
    const char *m_line = strstr(sdp_body, "m=audio");
    if (!c_line || !m_line) {
        ESP_LOGW(TAG, "SDP missing c= or m=audio");
        return false;
    }

    char ip_str[64];
    if (sscanf(c_line, "c=IN IP4 %63s", ip_str) != 1 || !ipaddr_aton(ip_str, remote_ip)) {
        ESP_LOGW(TAG, "SDP c= parse failed");
        return false;
    }

    unsigned int port = 0;
    int n = 0;
    if (sscanf(m_line, "m=audio %u RTP/AVP %n", &port, &n) < 1 || port == 0) {
        ESP_LOGW(TAG, "SDP m= port parse failed");
        return false;
    }
    *remote_port = (uint16_t)port;

    // Walk the payload-type list and pick the highest-ranked one we support.
    int best_pt = -1, best_rank = 0;
    const char *p = m_line + n;
    while (*p && *p != '\r' && *p != '\n') {
        int pt = -1, adv = 0;
        if (sscanf(p, " %d%n", &pt, &adv) == 1 && adv > 0) {
            if (pt_supported(pt) && pt_rank(pt) > best_rank) {
                best_rank = pt_rank(pt);
                best_pt = pt;
            }
            p += adv;
        } else {
            break;
        }
    }
    if (best_pt < 0) {
        ESP_LOGW(TAG, "No mutually supported codec in SDP");
        return false;
    }
    *chosen_pt = best_pt;
    ESP_LOGI(TAG, "SDP remote %s:%u, codec PT=%d", ip_str, *remote_port, best_pt);
    return true;
}

static void start_call_audio(sip_client_t *client) {
    if (client->audio) {
        audio_pipeline_start(client->audio, client->local_rtp_port);
    }
}

static void process_incoming_sip(sip_client_t *client, char *buffer, int len, struct sockaddr_in *remote_addr) {
    (void)len;
    char method[32] = {0};
    char cseq_hdr[64] = {0};
    char call_id_hdr[128] = {0};
    char from_hdr[256] = {0};
    char to_hdr[256] = {0};
    char content_type_hdr[64] = {0};
    uint32_t cseq_num = 0;
    char cseq_method[32] = {0};
    int status_code = 0;

    // Remember where this datagram came from (for routing responses/ACKs).
    client->last_remote_addr = *remote_addr;

    if (strncmp(buffer, "SIP/2.0", 7) == 0) {
        // -------- Response --------
        if (sscanf(buffer, "SIP/2.0 %d", &status_code) != 1) return;

        parse_header(buffer, "CSeq", cseq_hdr, sizeof(cseq_hdr));
        parse_header(buffer, "Call-ID", call_id_hdr, sizeof(call_id_hdr));
        parse_header(buffer, "To", to_hdr, sizeof(to_hdr));
        sscanf(cseq_hdr, "%" SCNu32 " %31s", &cseq_num, cseq_method);

        if (strcasecmp(cseq_method, "REGISTER") == 0) {
            if (status_code == 200) {
                ESP_LOGI(TAG, "Registration OK");
                client->is_registered = true;
                client->has_auth_info = false; // reset until next challenge
                xEventGroupSetBits(client->event_group, client->registered_bit);
                xTimerStart(client->registration_timer, portMAX_DELAY);
                if (client->app_callbacks.on_registration_status) client->app_callbacks.on_registration_status(true);
            } else if ((status_code == 401 || status_code == 407) && !client->has_auth_info) {
                char auth_val[300] = {0};
                const char *hdr = (status_code == 401) ? "WWW-Authenticate" : "Proxy-Authenticate";
                if (parse_header(buffer, hdr, auth_val, sizeof(auth_val))) {
                    client->auth_is_proxy = (status_code == 407);
                    parse_auth_challenge(client, auth_val);
                    client->has_auth_info = true;
                    ESP_LOGI(TAG, "Auth challenge: realm=%s qop=%s", client->auth_realm, client->auth_qop);
                    send_register(client, false);
                }
            } else {
                ESP_LOGE(TAG, "Registration failed: %d", status_code);
                client->is_registered = false;
                xEventGroupClearBits(client->event_group, client->registered_bit);
                if (client->app_callbacks.on_registration_status) client->app_callbacks.on_registration_status(false);
            }
        } else if (strcasecmp(cseq_method, "INVITE") == 0) {
            if (client->call_state >= SIP_CALL_STATE_INVITING &&
                strcmp(client->current_call_id, call_id_hdr) == 0) {

                if (status_code >= 100 && status_code < 200) {
                    ESP_LOGI(TAG, "Provisional %d", status_code);
                    if (status_code == 180 || status_code == 183) client->call_state = SIP_CALL_STATE_RINGING;
                } else if (status_code == 200) {
                    // Capture To-tag.
                    char *tag = strstr(to_hdr, ";tag=");
                    if (tag) { sscanf(tag + 5, "%63[^;\r\n]", client->current_to_tag); }

                    char *sdp_body = strstr(buffer, "\r\n\r\n");
                    ip_addr_t rip; uint16_t rport; int pt = -1;
                    if (sdp_body && parse_sdp(client, sdp_body + 4, &rip, &rport, &pt)) {
                        client->remote_rtp_addr.sin_family = AF_INET;
                        client->remote_rtp_addr.sin_port = htons(rport);
                        client->remote_rtp_addr.sin_addr.s_addr = ipaddr_to_u32(&rip);
                        client->negotiated_pt = pt;
                        client->call_state = SIP_CALL_STATE_ACTIVE;
                        send_ack(client, &client->last_remote_addr);
                        start_call_audio(client);
                        if (client->app_callbacks.on_call_answered) client->app_callbacks.on_call_answered(client->current_call_id);
                    } else {
                        ESP_LOGE(TAG, "Bad SDP in 200 OK; sending ACK+BYE");
                        send_ack(client, &client->last_remote_addr);
                        send_bye(client);
                        client->call_state = SIP_CALL_STATE_IDLE;
                    }
                } else if (status_code == 401 || status_code == 407) {
                    // Authenticated outbound call: ACK the failure then retry with auth.
                    send_ack(client, &client->last_remote_addr);
                    if (!client->has_auth_info) {
                        char auth_val[300] = {0};
                        const char *hdr = (status_code == 401) ? "WWW-Authenticate" : "Proxy-Authenticate";
                        if (parse_header(buffer, hdr, auth_val, sizeof(auth_val))) {
                            client->auth_is_proxy = (status_code == 407);
                            parse_auth_challenge(client, auth_val);
                            client->has_auth_info = true;
                            send_invite(client, client->current_remote_uri);
                        }
                    } else {
                        client->call_state = SIP_CALL_STATE_IDLE;
                    }
                } else {
                    ESP_LOGW(TAG, "INVITE failed %d", status_code);
                    send_ack(client, &client->last_remote_addr);
                    char ended_id[128];
                    strncpy(ended_id, client->current_call_id, sizeof(ended_id) - 1);
                    ended_id[sizeof(ended_id) - 1] = '\0';
                    client->call_state = SIP_CALL_STATE_IDLE;
                    client->current_call_id[0] = '\0';
                    if (client->app_callbacks.on_call_ended) client->app_callbacks.on_call_ended(ended_id);
                }
            }
        } else if (strcasecmp(cseq_method, "BYE") == 0) {
            client->call_state = SIP_CALL_STATE_IDLE;
            client->current_call_id[0] = '\0';
        }

    } else {
        // -------- Request --------
        if (sscanf(buffer, "%31s", method) != 1) return;

        parse_header(buffer, "CSeq", client->last_cseq, sizeof(client->last_cseq));
        parse_header(buffer, "Call-ID", client->last_call_id, sizeof(client->last_call_id));
        parse_header(buffer, "From", client->last_from, sizeof(client->last_from));
        parse_header(buffer, "To", client->last_to, sizeof(client->last_to));
        parse_header(buffer, "Via", client->last_via, sizeof(client->last_via));
        parse_header(buffer, "From", from_hdr, sizeof(from_hdr));
        parse_header(buffer, "Call-ID", call_id_hdr, sizeof(call_id_hdr));

        if (strcmp(method, "INVITE") == 0) {
            if (client->call_state != SIP_CALL_STATE_IDLE) {
                send_sip_response(client, 486, "Busy Here", NULL);
                return;
            }
            strncpy(client->current_call_id, client->last_call_id, sizeof(client->current_call_id) - 1);
            strncpy(client->current_remote_uri, from_hdr, sizeof(client->current_remote_uri) - 1);
            // Extract bare URI <...> from From for dialog target.
            char *lt = strchr(client->current_remote_uri, '<');
            char *gt = lt ? strchr(lt, '>') : NULL;
            if (lt && gt) { *gt = '\0'; memmove(client->current_remote_uri, lt + 1, strlen(lt + 1) + 1); }
            snprintf(client->current_to_tag, sizeof(client->current_to_tag), "%08" PRIx32, esp_random());

            char *sdp_body = strstr(buffer, "\r\n\r\n");
            ip_addr_t rip; uint16_t rport; int pt = -1;
            if (sdp_body && parse_sdp(client, sdp_body + 4, &rip, &rport, &pt)) {
                client->remote_rtp_addr.sin_family = AF_INET;
                client->remote_rtp_addr.sin_port = htons(rport);
                client->remote_rtp_addr.sin_addr.s_addr = ipaddr_to_u32(&rip);
                client->negotiated_pt = pt;
                client->call_state = SIP_CALL_STATE_INCOMING;

                send_sip_response(client, 100, "Trying", NULL);
                send_sip_response(client, 180, "Ringing", client->current_to_tag);

                if (client->app_callbacks.on_incoming_call)
                    client->app_callbacks.on_incoming_call(from_hdr, client->current_call_id);
#ifdef CTRL_METHOD_AUTO
                ESP_LOGI(TAG, "Auto-answering");
                sip_client_answer_call(client);
#endif
            } else {
                send_sip_response(client, 488, "Not Acceptable Here", NULL);
                client->call_state = SIP_CALL_STATE_IDLE;
            }

        } else if (strcmp(method, "ACK") == 0) {
            if (client->call_state == SIP_CALL_STATE_CONNECTING) {
                ESP_LOGI(TAG, "ACK received -> ACTIVE");
                client->call_state = SIP_CALL_STATE_ACTIVE;
                start_call_audio(client);
                if (client->app_callbacks.on_call_answered) client->app_callbacks.on_call_answered(client->current_call_id);
            }

        } else if (strcmp(method, "BYE") == 0) {
            if (client->call_state >= SIP_CALL_STATE_CONNECTING && client->call_state <= SIP_CALL_STATE_ACTIVE) {
                char ended_id[128];
                strncpy(ended_id, client->current_call_id, sizeof(ended_id) - 1);
                ended_id[sizeof(ended_id) - 1] = '\0';
                if (client->audio) audio_pipeline_stop(client->audio);
                send_sip_response(client, 200, "OK", NULL);
                client->call_state = SIP_CALL_STATE_IDLE;
                client->current_call_id[0] = '\0';
                client->negotiated_pt = -1;
                if (client->app_callbacks.on_call_ended) client->app_callbacks.on_call_ended(ended_id);
            } else {
                send_sip_response(client, 481, "Call/Transaction Does Not Exist", NULL);
            }

        } else if (strcmp(method, "CANCEL") == 0) {
            if (client->call_state == SIP_CALL_STATE_INCOMING || client->call_state == SIP_CALL_STATE_RINGING) {
                char ended_id[128];
                strncpy(ended_id, client->current_call_id, sizeof(ended_id) - 1);
                ended_id[sizeof(ended_id) - 1] = '\0';
                send_sip_response(client, 200, "OK", NULL);
                send_sip_response(client, 487, "Request Terminated", client->current_to_tag);
                client->call_state = SIP_CALL_STATE_IDLE;
                client->current_call_id[0] = '\0';
                if (client->app_callbacks.on_call_ended) client->app_callbacks.on_call_ended(ended_id);
            } else {
                send_sip_response(client, 481, "Call/Transaction Does Not Exist", NULL);
            }

        } else if (strcmp(method, "INFO") == 0) {
            parse_header(buffer, "Content-Type", content_type_hdr, sizeof(content_type_hdr));
            if (strstr(content_type_hdr, "dtmf")) {
                char *body = strstr(buffer, "\r\n\r\n");
                if (body) {
                    char signal[16] = {0};
                    if (parse_header(body + 4, "Signal", signal, sizeof(signal))) {
                        ESP_LOGI(TAG, "DTMF: %s", signal);
                    }
                }
            }
            send_sip_response(client, 200, "OK", NULL);

        } else if (strcmp(method, "OPTIONS") == 0) {
            send_sip_response(client, 200, "OK", NULL);
        } else {
            send_sip_response(client, 501, "Not Implemented", NULL);
        }
    }
}
