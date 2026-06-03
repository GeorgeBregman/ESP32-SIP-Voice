// g711_codec.h
#ifndef G711_CODEC_H
#define G711_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    G711_ULAW,
    G711_ALAW
} g711_type_t;

/**
 * @brief Encode 16-bit signed linear PCM samples to G.711 (A-law or u-law).
 *
 * @param g711_data   Output buffer for G.711 encoded data (1 byte per sample).
 * @param pcm_data    Input buffer with 16-bit signed linear PCM samples.
 * @param sample_count Number of samples to encode.
 * @param type        G711_ULAW or G711_ALAW.
 * @return Number of bytes written to g711_data (== sample_count).
 */
size_t g711_encode(uint8_t* g711_data, const int16_t* pcm_data, size_t sample_count, g711_type_t type);

/**
 * @brief Decode G.711 (A-law or u-law) to 16-bit signed linear PCM.
 *
 * @param pcm_data    Output buffer for 16-bit signed linear PCM samples.
 * @param g711_data   Input buffer with G.711 encoded data.
 * @param sample_count Number of samples to decode.
 * @param type        G711_ULAW or G711_ALAW.
 * @return Number of BYTES written to pcm_data (== sample_count * 2).
 */
size_t g711_decode(int16_t* pcm_data, const uint8_t* g711_data, size_t sample_count, g711_type_t type);

/* Single-sample helpers (CCITT G.711, 16-bit linear input). */
uint8_t g711_linear2ulaw(int16_t pcm_val);
int16_t g711_ulaw2linear(uint8_t ulaw_val);
uint8_t g711_linear2alaw(int16_t pcm_val);
int16_t g711_alaw2linear(uint8_t alaw_val);

#ifdef __cplusplus
}
#endif

#endif // G711_CODEC_H
