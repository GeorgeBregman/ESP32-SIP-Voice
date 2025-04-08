// g711_codec.h
#ifndef G711_CODEC_H
#define G711_CODEC_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    G711_ULAW,
    G711_ALAW
} g711_type_t;

/**
 * @brief Encode linear PCM samples to G.711 (A-law or u-law).
 *
 * @param g711_data Output buffer for G.711 encoded data.
 * @param pcm_data Input buffer with 16-bit signed linear PCM samples.
 * @param sample_count Number of samples to encode.
 * @param type G711_ULAW or G711_ALAW.
 * @return Number of bytes written to g711_data (should be sample_count).
 */
size_t g711_encode(uint8_t* g711_data, const int16_t* pcm_data, size_t sample_count, g711_type_t type);

/**
 * @brief Decode G.711 (A-law or u-law) samples to linear PCM.
 *
 * @param pcm_data Output buffer for 16-bit signed linear PCM samples.
 * @param g711_data Input buffer with G.711 encoded data.
 * @param sample_count Number of samples to decode.
 * @param type G711_ULAW or G711_ALAW.
 * @return Number of bytes written to pcm_data (should be sample_count * 2).
 */
size_t g711_decode(int16_t* pcm_data, const uint8_t* g711_data, size_t sample_count, g711_type_t type);

// You might also add single sample conversion functions if needed
// uint8_t linear16_to_ulaw(int16_t pcm_val);
// int16_t ulaw_to_linear16(uint8_t ulaw_val);
// uint8_t linear16_to_alaw(int16_t pcm_val);
// int16_t alaw_to_linear16(uint8_t alaw_val);

#endif // G711_CODEC_H

// g711_codec.c
// Standard G.711 implementation. Find robust C code online (e.g., from SpanDSP, Asterisk sources, or similar)
// and place it here, implementing the functions declared in the header.
// Make sure it's licensed compatibly with your project.
#include "g711_codec.h"

// --- Placeholder Implementation (Replace with actual G.711 code) ---

// Example: Very basic (and likely incorrect/incomplete) u-law placeholders
static uint8_t linear16_to_ulaw(int16_t pcm_val) {
    // *** REPLACE WITH REAL ULAW COMPRESSION ALGORITHM ***
    // This is just a dummy conversion
    int16_t mask = 0xFF80; // ~ top 8 bits
    if (pcm_val < 0) {
        pcm_val = -pcm_val;
        mask = 0x7F80;
    }
    uint8_t ulaw = (uint8_t)((pcm_val >> 7) & 0x7F); // Highly simplified
    if (mask == 0x7F80) ulaw |= 0x80; // Sign bit
    return ~ulaw; // Inverted bits in u-law standard
}

static int16_t ulaw_to_linear16(uint8_t ulaw_val) {
    // *** REPLACE WITH REAL ULAW EXPANSION ALGORITHM ***
     ulaw_val = ~ulaw_val; // Invert bits
     int16_t sign = (ulaw_val & 0x80) ? -1 : 1;
     int16_t linear = ((int16_t)(ulaw_val & 0x7F)) << 7; // Highly simplified expansion
     return sign * linear;
}


size_t g711_encode(uint8_t* g711_data, const int16_t* pcm_data, size_t sample_count, g711_type_t type) {
     if (!g711_data || !pcm_data) return 0;
     // TODO: Add A-law support
     if (type == G711_ULAW) {
         for (size_t i = 0; i < sample_count; ++i) {
             g711_data[i] = linear16_to_ulaw(pcm_data[i]);
         }
         return sample_count;
     }
     return 0; // Unsupported type
}


size_t g711_decode(int16_t* pcm_data, const uint8_t* g711_data, size_t sample_count, g711_type_t type) {
     if (!g711_data || !pcm_data) return 0;
      // TODO: Add A-law support
     if (type == G711_ULAW) {
         for (size_t i = 0; i < sample_count; ++i) {
             pcm_data[i] = ulaw_to_linear16(g711_data[i]);
         }
         return sample_count * 2; // 2 bytes per 16-bit sample
     }
     return 0; // Unsupported type
}
