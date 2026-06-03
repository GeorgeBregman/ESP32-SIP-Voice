// g722_codec.c
//
// ITU-T G.722 sub-band ADPCM (64 kbit/s mode).
//
// Adapted from Steve Underwood's reference implementation, which the author
// explicitly placed in the PUBLIC DOMAIN (as imported by Asterisk/spandsp),
// hence it is compatible with this project's MIT license. Encodes/decodes
// 16-bit, 16 kHz linear PCM. Two PCM samples in -> one G.722 byte out.
#include "g722_codec.h"

static int saturate(int amp) {
    if (amp > 32767)  return 32767;
    if (amp < -32768) return -32768;
    return amp;
}

// --- ITU-T G.722 constant tables ---
static const int wl[8]    = { -60, -30, 58, 172, 334, 538, 1198, 3042 };
static const int rl42[16] = { 0, 7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3, 2, 1, 0 };
static const int ilb[32]  = {
    2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383,
    2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
    2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371,
    3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008
};
static const int wh[3]   = { 0, -214, 798 };
static const int rh2[4]  = { 2, 1, 2, 1 };
static const int qm2[4]  = { -7408, -1616, 7408, 1616 };
static const int qm4[16] = {
    0, -20456, -12896, -8968, -6288, -4240, -2584, -1200,
    20456, 12896, 8968, 6288, 4240, 2584, 1200, 0
};
static const int qm6[64] = {
    -136, -136, -136, -136, -24808, -21904, -19008, -16704,
    -14984, -13512, -12280, -11192, -10232, -9360, -8576, -7856,
    -7192, -6576, -6000, -5456, -4944, -4464, -4008, -3576,
    -3168, -2776, -2400, -2032, -1688, -1360, -1040, -728,
    24808, 21904, 19008, 16704, 14984, 13512, 12280, 11192,
    10232, 9360, 8576, 7856, 7192, 6576, 6000, 5456,
    4944, 4464, 4008, 3576, 3168, 2776, 2400, 2032,
    1688, 1360, 1040, 728, 432, 136, -432, -136
};
static const int q6[32] = {
    0, 35, 72, 110, 150, 190, 233, 276,
    323, 370, 422, 473, 530, 587, 650, 714,
    786, 858, 940, 1023, 1121, 1219, 1339, 1458,
    1612, 1765, 1980, 2195, 2557, 2919, 0, 0
};
static const int iln[32] = {
    0, 63, 62, 31, 30, 29, 28, 27,
    26, 25, 24, 23, 22, 21, 20, 19,
    18, 17, 16, 15, 14, 13, 12, 11,
    10, 9, 8, 7, 6, 5, 4, 0
};
static const int ilp[32] = {
    0, 61, 60, 59, 58, 57, 56, 55,
    54, 53, 52, 51, 50, 49, 48, 47,
    46, 45, 44, 43, 42, 41, 40, 39,
    38, 37, 36, 35, 34, 33, 32, 0
};
static const int ihn[3] = { 0, 1, 0 };
static const int ihp[3] = { 0, 3, 2 };
static const int qmf_coeffs[12] = {
    3, -11, -11, 53, 12, -156, 32, 362, -210, -805, 951, 3876
};

// Adaptive predictor update (shared by encoder and decoder).
static void block4(g722_band_t *b, int d) {
    int wd1, wd2, wd3, i;

    b->d[0] = d;
    b->r[0] = saturate(b->s + d);
    b->p[0] = saturate(b->sz + d);

    // UPPOL2
    for (i = 0; i < 3; i++) b->sg[i] = b->p[i] >> 15;
    wd1 = saturate(b->a[1] << 2);
    wd2 = (b->sg[0] == b->sg[1]) ? -wd1 : wd1;
    if (wd2 > 32767) wd2 = 32767;
    wd3 = (wd2 >> 7) + ((b->sg[0] == b->sg[2]) ? 128 : -128) + ((b->a[2] * 32512) >> 15);
    if (wd3 > 12288) wd3 = 12288;
    else if (wd3 < -12288) wd3 = -12288;
    b->ap[2] = wd3;

    // UPPOL1
    b->sg[0] = b->p[0] >> 15;
    b->sg[1] = b->p[1] >> 15;
    wd1 = (b->sg[0] == b->sg[1]) ? 192 : -192;
    wd2 = (b->a[1] * 32640) >> 15;
    b->ap[1] = saturate(wd1 + wd2);
    wd3 = saturate(15360 - b->ap[2]);
    if (b->ap[1] > wd3) b->ap[1] = wd3;
    else if (b->ap[1] < -wd3) b->ap[1] = -wd3;

    // UPZERO
    wd1 = (d == 0) ? 0 : 128;
    b->sg[0] = d >> 15;
    for (i = 1; i < 7; i++) {
        b->sg[i] = b->d[i] >> 15;
        wd2 = (b->sg[i] == b->sg[0]) ? wd1 : -wd1;
        wd3 = (b->b[i] * 32640) >> 15;
        b->bp[i] = saturate(wd2 + wd3);
    }

    // DELAYA
    for (i = 6; i > 0; i--) {
        b->d[i] = b->d[i - 1];
        b->b[i] = b->bp[i];
    }
    for (i = 2; i > 0; i--) {
        b->r[i] = b->r[i - 1];
        b->p[i] = b->p[i - 1];
        b->a[i] = b->ap[i];
    }

    // FILTEP
    wd1 = saturate(b->r[1] + b->r[1]);
    wd1 = (b->a[1] * wd1) >> 15;
    wd2 = saturate(b->r[2] + b->r[2]);
    wd2 = (b->a[2] * wd2) >> 15;
    b->sp = saturate(wd1 + wd2);

    // FILTEZ
    b->sz = 0;
    for (i = 6; i > 0; i--) {
        wd1 = saturate(b->d[i] + b->d[i]);
        b->sz += (b->b[i] * wd1) >> 15;
    }
    b->sz = saturate(b->sz);

    // PREDIC
    b->s = saturate(b->sp + b->sz);
}

static void state_init(g722_band_t band[2]) {
    band[0].det = 32;
    band[1].det = 8;
}

void g722_encode_init(g722_encode_state_t *s, int rate, int options) {
    (void)rate; (void)options;
    for (int i = 0; i < (int)(sizeof(*s) / sizeof(int)); i++) ((int*)s)[i] = 0;
    state_init(s->band);
    s->bits_per_sample = 8;
}

void g722_decode_init(g722_decode_state_t *s, int rate, int options) {
    (void)rate; (void)options;
    for (int i = 0; i < (int)(sizeof(*s) / sizeof(int)); i++) ((int*)s)[i] = 0;
    state_init(s->band);
    s->bits_per_sample = 8;
}

int g722_encode(g722_encode_state_t *s, uint8_t g722_data[], const int16_t amp[], int len) {
    int xlow, xhigh;
    int el, wd, wd1, wd2, wd3, ril, ilow, il4;
    int eh, mih, ihigh, ih2;
    int dlow, dhigh;
    int i, j;
    int out = 0;
    int sumeven, sumodd;

    for (j = 0; j + 1 < len; ) {
        // 24-tap QMF analysis -> low/high sub-bands
        for (i = 0; i < 22; i++) s->x[i] = s->x[i + 2];
        s->x[22] = amp[j++];
        s->x[23] = amp[j++];
        sumodd = sumeven = 0;
        for (i = 0; i < 12; i++) {
            sumodd  += s->x[2 * i]     * qmf_coeffs[i];
            sumeven += s->x[2 * i + 1] * qmf_coeffs[11 - i];
        }
        xlow  = (sumeven + sumodd) >> 14;
        xhigh = (sumeven - sumodd) >> 14;

        // ---- Lower sub-band ----
        el = saturate(xlow - s->band[0].s);
        wd = (el >= 0) ? el : -(el + 1);
        for (i = 1; i < 30; i++) {
            wd1 = (q6[i] * s->band[0].det) >> 12;
            if (wd < wd1) break;
        }
        ilow = (el < 0) ? iln[i] : ilp[i];

        ril = ilow >> 2;
        wd2 = qm4[ril];
        dlow = (s->band[0].det * wd2) >> 15;

        il4 = rl42[ril];
        wd = (s->band[0].nb * 127) >> 7;
        wd += wl[il4];
        if (wd < 0) wd = 0;
        else if (wd > 18432) wd = 18432;
        s->band[0].nb = wd;

        wd1 = (s->band[0].nb >> 6) & 31;
        wd2 = s->band[0].nb >> 11;
        wd3 = (wd2 < 0) ? (ilb[wd1] >> -wd2) : (ilb[wd1] << wd2);
        s->band[0].det = wd3 << 2;
        block4(&s->band[0], dlow);

        // ---- Upper sub-band ----
        eh = saturate(xhigh - s->band[1].s);
        wd = (eh >= 0) ? eh : -(eh + 1);
        wd1 = (564 * s->band[1].det) >> 12;
        mih = (wd >= wd1) ? 2 : 1;
        ihigh = (eh < 0) ? ihn[mih] : ihp[mih];

        wd2 = qm2[ihigh];
        dhigh = (s->band[1].det * wd2) >> 15;

        ih2 = rh2[ihigh];
        wd = (s->band[1].nb * 127) >> 7;
        wd += wh[ih2];
        if (wd < 0) wd = 0;
        else if (wd > 22528) wd = 22528;
        s->band[1].nb = wd;

        wd1 = (s->band[1].nb >> 6) & 31;
        wd2 = s->band[1].nb >> 11;
        wd3 = (wd2 < 0) ? (ilb[wd1] >> -wd2) : (ilb[wd1] << wd2);
        s->band[1].det = wd3 << 2;
        block4(&s->band[1], dhigh);

        g722_data[out++] = (uint8_t)((ihigh << 6) | ilow);
    }
    return out;
}

int g722_decode(g722_decode_state_t *s, int16_t amp[], const uint8_t g722_data[], int len) {
    int rlow, rhigh;
    int ilow, ihigh, ril, il4, ih2;
    int dlow, dlowt, dhigh;
    int wd1, wd2, wd3, wd;
    int i, j;
    int out = 0;
    int xout1, xout2;

    for (j = 0; j < len; j++) {
        int code = g722_data[j];
        ilow  = code & 0x3F;
        ihigh = (code >> 6) & 0x03;

        // ---- Lower sub-band ----
        ril = ilow >> 2;

        // Reconstructed output uses full 6-bit dequantizer.
        wd2 = qm6[ilow];
        dlow = (s->band[0].det * wd2) >> 15;
        rlow = saturate(s->band[0].s + dlow);
        if (rlow > 16383) rlow = 16383;
        else if (rlow < -16384) rlow = -16384;

        // Predictor adaptation uses the 4-bit dequantizer.
        wd2 = qm4[ril];
        dlowt = (s->band[0].det * wd2) >> 15;

        il4 = rl42[ril];
        wd = (s->band[0].nb * 127) >> 7;
        wd += wl[il4];
        if (wd < 0) wd = 0;
        else if (wd > 18432) wd = 18432;
        s->band[0].nb = wd;

        wd1 = (s->band[0].nb >> 6) & 31;
        wd2 = s->band[0].nb >> 11;
        wd3 = (wd2 < 0) ? (ilb[wd1] >> -wd2) : (ilb[wd1] << wd2);
        s->band[0].det = wd3 << 2;
        block4(&s->band[0], dlowt);

        // ---- Upper sub-band ----
        wd2 = qm2[ihigh];
        dhigh = (s->band[1].det * wd2) >> 15;
        rhigh = saturate(dhigh + s->band[1].s);
        if (rhigh > 16383) rhigh = 16383;
        else if (rhigh < -16384) rhigh = -16384;

        ih2 = rh2[ihigh];
        wd = (s->band[1].nb * 127) >> 7;
        wd += wh[ih2];
        if (wd < 0) wd = 0;
        else if (wd > 22528) wd = 22528;
        s->band[1].nb = wd;

        wd1 = (s->band[1].nb >> 6) & 31;
        wd2 = s->band[1].nb >> 11;
        wd3 = (wd2 < 0) ? (ilb[wd1] >> -wd2) : (ilb[wd1] << wd2);
        s->band[1].det = wd3 << 2;
        block4(&s->band[1], dhigh);

        // ---- QMF synthesis -> two 16 kHz samples ----
        for (i = 0; i < 22; i++) s->x[i] = s->x[i + 2];
        s->x[22] = rlow + rhigh;
        s->x[23] = rlow - rhigh;
        xout1 = xout2 = 0;
        for (i = 0; i < 12; i++) {
            xout2 += s->x[2 * i]     * qmf_coeffs[i];
            xout1 += s->x[2 * i + 1] * qmf_coeffs[11 - i];
        }
        amp[out++] = (int16_t)saturate(xout1 >> 11);
        amp[out++] = (int16_t)saturate(xout2 >> 11);
    }
    return out;
}
