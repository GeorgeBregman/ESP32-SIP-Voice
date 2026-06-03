// audio_pipeline.c
//
// Real-time full-duplex audio engine:
//   I2S mic  -> AEC -> encode (G.711/G.722/OPUS) -> RTP send
//   RTP recv -> jitter buffer -> decode -> I2S speaker
//
// The active codec is chosen at call setup from the SDP-negotiated RTP payload
// type, and the I2S sample rate is switched to match (8 k / 16 k / 48 k).
#include "audio_pipeline.h"
#include "app_config.h"
#include "config_manager.h"
#include "sip_client.h"          // for negotiated codec + remote RTP info
#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "g711_codec.h"
#include "rtp_handler.h"
#include "codec_driver.h"
#include "wake_word.h"
#include "aec_filter.h"
#ifdef USE_CODEC_G722
#include "g722_codec.h"
#endif
#ifdef USE_CODEC_OPUS
#include "opus_codec.h"
#endif

static const char* TAG = "AUDIO";

// Worst-case PCM frame: OPUS 20 ms @ 48 kHz = 960 samples. Size everything for
// that so a runtime codec switch never overflows a buffer.
#define AUDIO_BUF_SAMPLES   1024

// Idle (wake-word) capture rate. WakeNet models expect 16 kHz.
#if USE_WAKE_WORD
#define IDLE_SAMPLE_RATE    16000
#else
#define IDLE_SAMPLE_RATE    AUDIO_SAMPLE_RATE
#endif

#define I2S_DMA_BUF_COUNT   6
#define I2S_DMA_BUF_LEN     320   // samples per DMA buffer

typedef enum { AC_PCMU, AC_PCMA, AC_G722, AC_OPUS } active_codec_t;

typedef struct audio_pipeline_s {
    TaskHandle_t audio_io_task_handle;
    volatile bool is_running;
    rtp_session_handle_t rtp_session;
    void* sip_client;                 // sip_client_handle_t (opaque here)
    int   current_i2s_rate;

    active_codec_t codec;
    int  codec_audio_rate;
    int  samples_per_frame;           // PCM samples per 20 ms at codec rate
    int  rtp_ts_increment;
    int  rtp_payload_type;

    aec_filter_t aec;
#ifdef USE_CODEC_G722
    g722_encode_state_t g722_enc;
    g722_decode_state_t g722_dec;
#endif
#ifdef USE_CODEC_OPUS
    opus_encode_state_t opus_enc;
    opus_decode_state_t opus_dec;
#endif

    // Working buffers
    int16_t mic_pcm[AUDIO_BUF_SAMPLES];
    int16_t clean_pcm[AUDIO_BUF_SAMPLES];
    int16_t play_pcm[AUDIO_BUF_SAMPLES];
    int16_t ref_pcm[AUDIO_BUF_SAMPLES];   // last played frame (AEC reference)
    uint8_t enc_buf[RTP_MAX_PAYLOAD];
    uint8_t rx_buf[sizeof(rtp_header_t) + RTP_MAX_PAYLOAD];

    void (*wake_word_callback)(void);
} audio_pipeline_t;

static void audio_io_task(void* pvParameters);
static esp_err_t i2s_init(int sample_rate);
static esp_err_t i2s_deinit(void);
static void i2s_set_rate(audio_pipeline_t* p, int rate);

// ---- codec helpers ---------------------------------------------------------

static active_codec_t codec_from_pt(int pt) {
    switch (pt) {
        case 0: return AC_PCMU;
        case 8: return AC_PCMA;
#ifdef USE_CODEC_G722
        case 9: return AC_G722;
#endif
#ifdef USE_CODEC_OPUS
        case 96: return AC_OPUS;
#endif
        default: return AC_PCMU;
    }
}

static int codec_audio_rate(active_codec_t c) {
    switch (c) {
        case AC_G722: return 16000;
        case AC_OPUS: return 48000;
        default:      return 8000;
    }
}

static int codec_rtp_clock(active_codec_t c) {
    // RFC 3551: G.722 uses an 8 kHz RTP clock despite 16 kHz audio.
    return (c == AC_OPUS) ? 48000 : 8000;
}

// Encode one PCM frame. Returns encoded byte count.
static int codec_encode(audio_pipeline_t* p, const int16_t* pcm, int samples) {
    switch (p->codec) {
        case AC_PCMU: return (int)g711_encode(p->enc_buf, pcm, samples, G711_ULAW);
        case AC_PCMA: return (int)g711_encode(p->enc_buf, pcm, samples, G711_ALAW);
#ifdef USE_CODEC_G722
        case AC_G722: return g722_encode(&p->g722_enc, p->enc_buf, pcm, samples);
#endif
#ifdef USE_CODEC_OPUS
        case AC_OPUS: return opus_encode_frame(&p->opus_enc, p->enc_buf, pcm, samples);
#endif
        default: return 0;
    }
}

// Decode one encoded frame into pcm. Returns PCM sample count.
static int codec_decode(audio_pipeline_t* p, const uint8_t* in, int in_len, int16_t* pcm) {
    switch (p->codec) {
        case AC_PCMU: g711_decode(pcm, in, in_len, G711_ULAW); return in_len;
        case AC_PCMA: g711_decode(pcm, in, in_len, G711_ALAW); return in_len;
#ifdef USE_CODEC_G722
        case AC_G722: return g722_decode(&p->g722_dec, pcm, in, in_len);
#endif
#ifdef USE_CODEC_OPUS
        case AC_OPUS: return opus_decode_frame(&p->opus_dec, pcm, in, in_len);
#endif
        default: return 0;
    }
}

// ---- lifecycle -------------------------------------------------------------

audio_pipeline_handle_t audio_pipeline_init(void) {
    audio_pipeline_t* p = (audio_pipeline_t*)calloc(1, sizeof(audio_pipeline_t));
    if (!p) {
        ESP_LOGE(TAG, "alloc failed");
        return NULL;
    }
    p->is_running = false;
    p->current_i2s_rate = IDLE_SAMPLE_RATE;

    if (i2s_init(IDLE_SAMPLE_RATE) != ESP_OK) { free(p); return NULL; }

    if (codec_init() != ESP_OK) {
        ESP_LOGE(TAG, "codec init failed");
        i2s_deinit();
        free(p);
        return NULL;
    }
    codec_set_mic_gain(24);
    codec_set_volume(80);

    if (xTaskCreate(audio_io_task, "audio_io", AUDIO_TASK_STACK_SIZE, p,
                    AUDIO_IO_TASK_PRIORITY, &p->audio_io_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        codec_deinit();
        i2s_deinit();
        free(p);
        return NULL;
    }

#if USE_WAKE_WORD
    wake_word_init();
#endif
    ESP_LOGI(TAG, "Audio pipeline initialized (idle rate %d Hz).", IDLE_SAMPLE_RATE);
    return p;
}

void audio_pipeline_set_wake_word_cb(audio_pipeline_handle_t handle, void (*cb)(void)) {
    audio_pipeline_t* p = (audio_pipeline_t*)handle;
    if (p) p->wake_word_callback = cb;
}

void audio_pipeline_set_sip_handle(audio_pipeline_handle_t handle, void* sip_handle) {
    audio_pipeline_t* p = (audio_pipeline_t*)handle;
    if (p) p->sip_client = sip_handle;
}

esp_err_t audio_pipeline_start(audio_pipeline_handle_t handle, uint16_t local_rtp_port) {
    audio_pipeline_t* p = (audio_pipeline_t*)handle;
    if (!p || p->is_running) return ESP_ERR_INVALID_STATE;
    if (!p->sip_client) {
        ESP_LOGE(TAG, "SIP handle not set");
        return ESP_FAIL;
    }

    // Pick the codec from SDP negotiation (fallback to compiled default).
    int pt = sip_client_get_negotiated_pt(p->sip_client);
    if (pt < 0) pt = AUDIO_CODEC_PAYLOAD_TYPE;
    p->codec            = codec_from_pt(pt);
    p->rtp_payload_type = pt;
    p->codec_audio_rate = codec_audio_rate(p->codec);
    p->samples_per_frame = p->codec_audio_rate * AUDIO_FRAME_MS / 1000;
    p->rtp_ts_increment  = codec_rtp_clock(p->codec) * AUDIO_FRAME_MS / 1000;

    // Init codec state + AEC for this call.
    aec_filter_init(&p->aec, p->codec_audio_rate, p->samples_per_frame);
#ifdef USE_CODEC_G722
    if (p->codec == AC_G722) {
        g722_encode_init(&p->g722_enc, 64000, 0);
        g722_decode_init(&p->g722_dec, 64000, 0);
    }
#endif
#ifdef USE_CODEC_OPUS
    if (p->codec == AC_OPUS) {
        opus_encode_init(&p->opus_enc, 48000, 1);
        opus_decode_init(&p->opus_dec, 48000, 1);
    }
#endif

    // Switch I2S to the codec's audio rate.
    i2s_set_rate(p, p->codec_audio_rate);

    p->rtp_session = rtp_session_create(local_rtp_port);
    if (!p->rtp_session) {
        ESP_LOGE(TAG, "RTP session create failed (port %d)", local_rtp_port);
        return ESP_FAIL;
    }

    memset(p->ref_pcm, 0, sizeof(p->ref_pcm));
    p->is_running = true;
    ESP_LOGI(TAG, "Pipeline started: codec PT=%d, rate=%d Hz, %d samples/frame",
             p->rtp_payload_type, p->codec_audio_rate, p->samples_per_frame);
    return ESP_OK;
}

esp_err_t audio_pipeline_stop(audio_pipeline_handle_t handle) {
    audio_pipeline_t* p = (audio_pipeline_t*)handle;
    if (!p || !p->is_running) return ESP_ERR_INVALID_STATE;

    p->is_running = false;
    vTaskDelay(pdMS_TO_TICKS(AUDIO_FRAME_MS * 2)); // let the task finish its frame

    if (p->rtp_session) {
        rtp_session_delete(p->rtp_session);
        p->rtp_session = NULL;
    }
    aec_filter_deinit(&p->aec);

    // Return to idle (wake-word) sample rate.
    i2s_set_rate(p, IDLE_SAMPLE_RATE);
    i2s_zero_dma_buffer(I2S_NUM);
    ESP_LOGI(TAG, "Pipeline stopped.");
    return ESP_OK;
}

void audio_pipeline_delete(audio_pipeline_handle_t handle) {
    audio_pipeline_t* p = (audio_pipeline_t*)handle;
    if (!p) return;
    if (p->is_running) audio_pipeline_stop(handle);
    if (p->audio_io_task_handle) vTaskDelete(p->audio_io_task_handle);
    codec_deinit();
    i2s_deinit();
    free(p);
}

// ---- the real-time task ----------------------------------------------------

static void audio_io_task(void* pv) {
    audio_pipeline_t* p = (audio_pipeline_t*)pv;
    size_t bytes_rw = 0;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t ticks_per_frame = pdMS_TO_TICKS(AUDIO_FRAME_MS);

    ip_addr_t remote_ip;
    uint16_t remote_port = 0;
    bool remote_valid = false;
    uint32_t ts = 0;
    bool first_pkt = true;

    ESP_LOGI(TAG, "Audio I/O task started.");

    while (1) {
        if (!p->is_running) {
            remote_valid = false;
            ts = 0;
            first_pkt = true;
#if USE_WAKE_WORD
            // Idle: capture 16 kHz and feed WakeNet.
            int idle_samples = IDLE_SAMPLE_RATE * AUDIO_FRAME_MS / 1000;
            if (i2s_read(I2S_NUM, p->mic_pcm, idle_samples * sizeof(int16_t), &bytes_rw, ticks_per_frame * 2) == ESP_OK
                && bytes_rw > 0) {
                if (wake_word_feed(p->mic_pcm, (int)bytes_rw)) {
                    if (p->wake_word_callback) p->wake_word_callback();
                }
            }
            vTaskDelayUntil(&last_wake, ticks_per_frame);
#else
            vTaskDelay(pdMS_TO_TICKS(100));
            last_wake = xTaskGetTickCount();
#endif
            continue;
        }

        int N = p->samples_per_frame;            // PCM samples this frame

        // Resolve remote RTP destination once it is known.
        if (!remote_valid && p->sip_client) {
            if (sip_client_get_remote_rtp_info(p->sip_client, &remote_ip, &remote_port) == ESP_OK) {
                ESP_LOGI(TAG, "Remote RTP: %s:%d", ipaddr_ntoa(&remote_ip), remote_port);
                remote_valid = true;
            }
        }

        // --- Capture mic -> AEC -> encode -> send ---
        if (i2s_read(I2S_NUM, p->mic_pcm, N * sizeof(int16_t), &bytes_rw, ticks_per_frame * 2) == ESP_OK
            && bytes_rw == (size_t)(N * sizeof(int16_t))) {

            // Echo cancellation using the last frame we played as reference.
            if (!aec_filter_process(&p->aec, p->mic_pcm, p->ref_pcm, p->clean_pcm)) {
                memcpy(p->clean_pcm, p->mic_pcm, N * sizeof(int16_t));
            }

            int enc_len = codec_encode(p, p->clean_pcm, N);
            if (enc_len > 0 && p->rtp_session && remote_valid) {
                rtp_send_packet(p->rtp_session, &remote_ip, remote_port,
                                p->rtp_payload_type, ts, first_pkt ? 1 : 0,
                                p->enc_buf, enc_len);
                ts += p->rtp_ts_increment;
                first_pkt = false;
            }
        }

        // --- Drain ALL pending RTP packets into the jitter buffer ---
        if (p->rtp_session) {
            int got;
            ip_addr_t sip; uint16_t sport; rtp_header_t rxh;
            do {
                got = rtp_receive_packet(p->rtp_session, p->rx_buf, sizeof(p->rx_buf), &sip, &sport, &rxh);
                if (got > 0) {
                    rtp_jitter_buffer_put(p->rtp_session, &rxh, p->rx_buf + sizeof(rtp_header_t), got);
                }
            } while (got > 0);

            // --- Pull one frame from the jitter buffer -> decode -> play ---
            rtp_header_t ph;
            uint8_t payload[RTP_MAX_PAYLOAD];
            int plen = rtp_jitter_buffer_get(p->rtp_session, &ph, payload, sizeof(payload));
            int play_samples;
            if (plen > 0) {
                play_samples = codec_decode(p, payload, plen, p->play_pcm);
            } else {
                // PLC / silence frame for the active codec.
                play_samples = N;
                memset(p->play_pcm, 0, N * sizeof(int16_t));
            }
            if (play_samples <= 0) play_samples = N;
            if (play_samples > AUDIO_BUF_SAMPLES) play_samples = AUDIO_BUF_SAMPLES;

            // Keep a copy as the AEC reference for the next captured frame.
            memcpy(p->ref_pcm, p->play_pcm, play_samples * sizeof(int16_t));

            i2s_write(I2S_NUM, p->play_pcm, play_samples * sizeof(int16_t), &bytes_rw, portMAX_DELAY);
        }

        vTaskDelayUntil(&last_wake, ticks_per_frame);
    }
}

// ---- I2S helpers -----------------------------------------------------------

static esp_err_t i2s_init(int sample_rate) {
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    hardware_settings_t hw;
    config_manager_load_hw(&hw);
    int bck  = hw.pin_i2s_bck  != -1 ? hw.pin_i2s_bck  : I2S_BCK_PIN;
    int ws   = hw.pin_i2s_ws   != -1 ? hw.pin_i2s_ws   : I2S_WS_PIN;
    int dout = hw.pin_i2s_dout != -1 ? hw.pin_i2s_dout : I2S_DATA_OUT_PIN;
    int din  = hw.pin_i2s_din  != -1 ? hw.pin_i2s_din  : I2S_DATA_IN_PIN;

    i2s_pin_config_t pins = {
        .mck_io_num = (hw.pin_i2s_mclk != -1 ? hw.pin_i2s_mclk : I2S_PIN_NO_CHANGE),
        .bck_io_num = bck,
        .ws_io_num = ws,
        .data_out_num = dout,
        .data_in_num = din
    };

    esp_err_t ret = i2s_driver_install(I2S_NUM, &cfg, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %d", ret);
        return ret;
    }
    ret = i2s_set_pin(I2S_NUM, &pins);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin failed: %d", ret);
        i2s_driver_uninstall(I2S_NUM);
        return ret;
    }
    i2s_zero_dma_buffer(I2S_NUM);
    ESP_LOGI(TAG, "I2S initialized @ %d Hz, 16-bit mono.", sample_rate);
    return ESP_OK;
}

static esp_err_t i2s_deinit(void) {
    return i2s_driver_uninstall(I2S_NUM);
}

static void i2s_set_rate(audio_pipeline_t* p, int rate) {
    if (p->current_i2s_rate == rate) return;
    esp_err_t ret = i2s_set_clk(I2S_NUM, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    if (ret == ESP_OK) {
        p->current_i2s_rate = rate;
        ESP_LOGI(TAG, "I2S sample rate -> %d Hz", rate);
    } else {
        ESP_LOGW(TAG, "i2s_set_clk(%d) failed: %d", rate, ret);
    }
}
