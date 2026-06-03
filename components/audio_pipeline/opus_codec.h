#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// OPUS Context structure (Stub)
typedef struct {
    void *encoder;
} opus_encode_state_t;

typedef struct {
    void *decoder;
} opus_decode_state_t;

// Initialize OPUS encoder
void opus_encode_init(opus_encode_state_t *s, int sample_rate, int channels);

// Initialize OPUS decoder
void opus_decode_init(opus_decode_state_t *s, int sample_rate, int channels);

// Encode PCM to OPUS
int opus_encode_frame(opus_encode_state_t *s, uint8_t *out_data, const int16_t *pcm_in, int frame_size);

// Decode OPUS to PCM
int opus_decode_frame(opus_decode_state_t *s, int16_t *pcm_out, const uint8_t *opus_in, int len);

#ifdef __cplusplus
}
#endif

#endif /* OPUS_CODEC_H */
