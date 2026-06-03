#ifndef G722_CODEC_H
#define G722_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// G.722 Context structure (Opaque in real implementation)
typedef struct {
    int32_t itf;
    // ... internal DSP state variables ...
} g722_encode_state_t;

typedef struct {
    int32_t itf;
    // ... internal DSP state variables ...
} g722_decode_state_t;

// Initialize the encoder state
void g722_encode_init(g722_encode_state_t *s, int rate, int options);

// Initialize the decoder state
void g722_decode_init(g722_decode_state_t *s, int rate, int options);

// Encode 16-bit PCM (16kHz) to G.722
// Returns the number of bytes written to g722_data
int g722_encode(g722_encode_state_t *s, uint8_t g722_data[], const int16_t amp[], int len);

// Decode G.722 to 16-bit PCM (16kHz)
// Returns the number of PCM samples written to amp
int g722_decode(g722_decode_state_t *s, int16_t amp[], const uint8_t g722_data[], int len);

#ifdef __cplusplus
}
#endif

#endif /* G722_CODEC_H */
