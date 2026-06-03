// g711_codec.c
//
// Full CCITT/ITU-T G.711 A-law and u-law companding.
//
// Based on the public-domain reference implementation by Sun Microsystems
// (Jutta Degener / Carsten Bormann, "g711.c"), which is the de-facto standard
// used by Asterisk, FreeSWITCH and spandsp. Input/output linear samples are
// 16-bit signed PCM.
#include "g711_codec.h"

#define SIGN_BIT    (0x80)  // Sign bit for a u-law/A-law byte
#define QUANT_MASK  (0x0F)  // Quantization field mask
#define NSEGS       (8)     // Number of u-law/A-law segments
#define SEG_SHIFT   (4)     // Left shift for segment number
#define SEG_MASK    (0x70)  // Segment field mask
#define BIAS        (0x84)  // u-law bias for linear samples
#define CLIP        (8159)

static const int16_t seg_uend[8] = { 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF };
static const int16_t seg_aend[8] = { 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF };

static int seg_search(int16_t val, const int16_t* table, int size) {
    int i;
    for (i = 0; i < size; i++) {
        if (val <= table[i]) {
            return i;
        }
    }
    return size;
}

uint8_t g711_linear2ulaw(int16_t pcm_val) {
    int16_t mask;
    int16_t seg;
    uint8_t uval;

    // Scale 16-bit linear PCM to the 14-bit range used by the algorithm.
    pcm_val = (int16_t)(pcm_val >> 2);
    if (pcm_val < 0) {
        pcm_val = (int16_t)-pcm_val;
        mask = 0x7F;
    } else {
        mask = 0xFF;
    }
    if (pcm_val > CLIP) {
        pcm_val = CLIP;
    }
    pcm_val = (int16_t)(pcm_val + (BIAS >> 2));

    seg = (int16_t)seg_search(pcm_val, seg_uend, NSEGS);

    if (seg >= 8) {
        return (uint8_t)(0x7F ^ mask);
    }
    uval = (uint8_t)((seg << 4) | ((pcm_val >> (seg + 1)) & 0x0F));
    return (uint8_t)(uval ^ mask);
}

int16_t g711_ulaw2linear(uint8_t u_val) {
    int16_t t;

    u_val = (uint8_t)~u_val;
    t = (int16_t)(((u_val & QUANT_MASK) << 3) + BIAS);
    t = (int16_t)(t << ((u_val & SEG_MASK) >> SEG_SHIFT));

    return (int16_t)((u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS));
}

uint8_t g711_linear2alaw(int16_t pcm_val) {
    int16_t mask;
    int16_t seg;
    uint8_t aval;

    pcm_val = (int16_t)(pcm_val >> 3);
    if (pcm_val >= 0) {
        mask = 0xD5;            // sign (7th) bit = 1
    } else {
        mask = 0x55;            // sign bit = 0
        pcm_val = (int16_t)(-pcm_val - 1);
    }

    seg = (int16_t)seg_search(pcm_val, seg_aend, NSEGS);

    if (seg >= 8) {
        return (uint8_t)(0x7F ^ mask);
    }
    aval = (uint8_t)(seg << SEG_SHIFT);
    if (seg < 2) {
        aval |= (uint8_t)((pcm_val >> 1) & QUANT_MASK);
    } else {
        aval |= (uint8_t)((pcm_val >> seg) & QUANT_MASK);
    }
    return (uint8_t)(aval ^ mask);
}

int16_t g711_alaw2linear(uint8_t a_val) {
    int16_t t;
    int16_t seg;

    a_val ^= 0x55;
    t = (int16_t)((a_val & QUANT_MASK) << 4);
    seg = (int16_t)(((unsigned)a_val & SEG_MASK) >> SEG_SHIFT);
    switch (seg) {
        case 0:
            t = (int16_t)(t + 8);
            break;
        case 1:
            t = (int16_t)(t + 0x108);
            break;
        default:
            t = (int16_t)(t + 0x108);
            t = (int16_t)(t << (seg - 1));
            break;
    }
    return (int16_t)((a_val & SIGN_BIT) ? t : -t);
}

size_t g711_encode(uint8_t* g711_data, const int16_t* pcm_data, size_t sample_count, g711_type_t type) {
    if (!g711_data || !pcm_data) {
        return 0;
    }
    if (type == G711_ULAW) {
        for (size_t i = 0; i < sample_count; ++i) {
            g711_data[i] = g711_linear2ulaw(pcm_data[i]);
        }
    } else { // G711_ALAW
        for (size_t i = 0; i < sample_count; ++i) {
            g711_data[i] = g711_linear2alaw(pcm_data[i]);
        }
    }
    return sample_count;
}

size_t g711_decode(int16_t* pcm_data, const uint8_t* g711_data, size_t sample_count, g711_type_t type) {
    if (!g711_data || !pcm_data) {
        return 0;
    }
    if (type == G711_ULAW) {
        for (size_t i = 0; i < sample_count; ++i) {
            pcm_data[i] = g711_ulaw2linear(g711_data[i]);
        }
    } else { // G711_ALAW
        for (size_t i = 0; i < sample_count; ++i) {
            pcm_data[i] = g711_alaw2linear(g711_data[i]);
        }
    }
    return sample_count * 2;
}
