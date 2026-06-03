#ifndef G722_CODEC_H
#define G722_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// G.722 sub-band ADPCM band state.
typedef struct {
    int s;
    int sp;
    int sz;
    int r[3];
    int a[3];
    int ap[3];
    int p[3];
    int d[7];
    int b[7];
    int bp[7];
    int sg[7];
    int nb;
    int det;
} g722_band_t;

typedef struct {
    int itu_test_mode;
    int packed;
    int eight_k;
    int bits_per_sample;
    int x[24];
    g722_band_t band[2];
    unsigned int in_buffer;
    int in_bits;
    unsigned int out_buffer;
    int out_bits;
} g722_encode_state_t;

typedef struct {
    int itu_test_mode;
    int packed;
    int eight_k;
    int bits_per_sample;
    int x[24];
    g722_band_t band[2];
    unsigned int in_buffer;
    int in_bits;
    unsigned int out_buffer;
    int out_bits;
} g722_decode_state_t;

// rate: 64000/56000/48000. options: bit0 = sample at 8k, bit1 = packed.
void g722_encode_init(g722_encode_state_t *s, int rate, int options);
void g722_decode_init(g722_decode_state_t *s, int rate, int options);

// Encode 16-bit/16kHz PCM (`len` samples) to G.722. Returns bytes written.
int g722_encode(g722_encode_state_t *s, uint8_t g722_data[], const int16_t amp[], int len);

// Decode `len` bytes of G.722 to 16-bit/16kHz PCM. Returns samples written.
int g722_decode(g722_decode_state_t *s, int16_t amp[], const uint8_t g722_data[], int len);

#ifdef __cplusplus
}
#endif

#endif /* G722_CODEC_H */
