// opus_codec.c
//
// Thin wrapper over the reference Xiph libopus codec.
//
// OPUS is only used by the PRO hardware profile. To keep the default build
// working without the (large) opus library, the real implementation is
// compiled only when <opus.h> is on the include path. Enable it by adding the
// dependency to components/audio_pipeline/idf_component.yml, e.g.:
//
//     dependencies:
//       chmorgan/esp32-libopus: "^1.4.0"   # or any IDF opus component
//
// When the library is absent this file degrades to a safe no-op so the
// firmware still links.
#include "opus_codec.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "OPUS_CODEC";

#if defined(__has_include)
#  if __has_include("opus.h")
#    define HAVE_LIBOPUS 1
#    include "opus.h"
#  endif
#endif

#ifdef HAVE_LIBOPUS

void opus_encode_init(opus_encode_state_t *s, int sample_rate, int channels) {
    int err = 0;
    OpusEncoder *enc = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !enc) {
        ESP_LOGE(TAG, "opus_encoder_create failed: %d", err);
        s->encoder = NULL;
        return;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(10));
    s->encoder = enc;
    ESP_LOGI(TAG, "OPUS encoder ready (%d Hz, %d ch)", sample_rate, channels);
}

void opus_decode_init(opus_decode_state_t *s, int sample_rate, int channels) {
    int err = 0;
    OpusDecoder *dec = opus_decoder_create(sample_rate, channels, &err);
    if (err != OPUS_OK || !dec) {
        ESP_LOGE(TAG, "opus_decoder_create failed: %d", err);
        s->decoder = NULL;
        return;
    }
    s->decoder = dec;
    ESP_LOGI(TAG, "OPUS decoder ready (%d Hz, %d ch)", sample_rate, channels);
}

int opus_encode_frame(opus_encode_state_t *s, uint8_t *out_data, const int16_t *pcm_in, int frame_size) {
    if (!s || !s->encoder) return 0;
    int n = opus_encode((OpusEncoder *)s->encoder, pcm_in, frame_size, out_data, 400);
    return (n > 0) ? n : 0;
}

int opus_decode_frame(opus_decode_state_t *s, int16_t *pcm_out, const uint8_t *opus_in, int len) {
    if (!s || !s->decoder) return 0;
    // frame_size cap = 20 ms @ 48 kHz = 960 samples; pass 0 packet -> PLC.
    int n = opus_decode((OpusDecoder *)s->decoder, opus_in, len, pcm_out, 960, 0);
    return (n > 0) ? n : 0;
}

#else // !HAVE_LIBOPUS

static bool warned = false;
void opus_encode_init(opus_encode_state_t *s, int sample_rate, int channels) {
    (void)sample_rate; (void)channels;
    if (s) s->encoder = NULL;
    if (!warned) { ESP_LOGW(TAG, "libopus not linked; OPUS disabled (see opus_codec.c)"); warned = true; }
}
void opus_decode_init(opus_decode_state_t *s, int sample_rate, int channels) {
    (void)sample_rate; (void)channels;
    if (s) s->decoder = NULL;
}
int opus_encode_frame(opus_encode_state_t *s, uint8_t *out_data, const int16_t *pcm_in, int frame_size) {
    (void)s; (void)out_data; (void)pcm_in; (void)frame_size; return 0;
}
int opus_decode_frame(opus_decode_state_t *s, int16_t *pcm_out, const uint8_t *opus_in, int len) {
    (void)s; (void)opus_in; (void)len;
    if (pcm_out) for (int i = 0; i < 960; i++) pcm_out[i] = 0;
    return 0;
}

#endif
