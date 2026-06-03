// aec_filter.c
//
// Acoustic Echo Cancellation via a Normalized Least-Mean-Squares (NLMS)
// adaptive FIR filter. Estimates the echo of the far-end (speaker) signal
// picked up by the microphone and subtracts it, leaving the near-end voice.
//
// This is a real, self-contained software AEC (no external DSP library). For
// the PRO tier you may instead route to esp-sr's hardware-accelerated AEC, but
// this works on plain ESP32/ESP32-S3 and needs no extra dependency.
#include "aec_filter.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "AEC_FILTER";

#define AEC_TAPS        128       // echo tail length in samples (~8 ms @16k)
#define AEC_MU          0.30f     // adaptation step
#define AEC_EPS         1.0e-6f
#define AEC_REF_THRESH  50.0f     // min far-end energy/sample to adapt (anti-divergence)

typedef struct {
    int   taps;
    int   frame;
    float mu;
    float *w;        // adaptive weights [taps]
    float *x;        // reference delay line [taps], newest at index 0
} aec_state_t;

bool aec_filter_init(aec_filter_t *aec, int sample_rate, int frame_size) {
    (void)sample_rate;
    if (!aec) return false;

    aec_state_t *st = (aec_state_t *)calloc(1, sizeof(aec_state_t));
    if (!st) { aec->aec_handle = NULL; return false; }

    st->taps  = AEC_TAPS;
    st->frame = frame_size;
    st->mu    = AEC_MU;
    st->w = (float *)calloc(st->taps, sizeof(float));
    st->x = (float *)calloc(st->taps, sizeof(float));
    if (!st->w || !st->x) {
        free(st->w); free(st->x); free(st);
        aec->aec_handle = NULL;
        return false;
    }
    aec->aec_handle = st;
    ESP_LOGI(TAG, "NLMS AEC ready: %d taps, frame %d", st->taps, frame_size);
    return true;
}

bool aec_filter_process(aec_filter_t *aec, const int16_t *mic_in, const int16_t *ref_in, int16_t *out) {
    if (!aec || !aec->aec_handle || !mic_in || !ref_in || !out) return false;
    aec_state_t *st = (aec_state_t *)aec->aec_handle;
    const int T = st->taps;

    for (int n = 0; n < st->frame; n++) {
        // Shift the newest reference sample into the delay line (index 0 = newest).
        for (int k = T - 1; k > 0; k--) st->x[k] = st->x[k - 1];
        st->x[0] = (float)ref_in[n];

        // Estimate echo y = w . x  and reference energy.
        float y = 0.0f, energy = 0.0f;
        for (int k = 0; k < T; k++) {
            y += st->w[k] * st->x[k];
            energy += st->x[k] * st->x[k];
        }

        float d = (float)mic_in[n];
        float e = d - y;                       // echo-cancelled (error) signal

        // Adapt only when there is meaningful far-end energy (avoids divergence
        // during near-end-only speech / silence).
        if (energy > AEC_REF_THRESH * T) {
            float step = st->mu * e / (energy + AEC_EPS);
            for (int k = 0; k < T; k++) st->w[k] += step * st->x[k];
        }

        int v = (int)lrintf(e);
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        out[n] = (int16_t)v;
    }
    return true;
}

void aec_filter_deinit(aec_filter_t *aec) {
    if (!aec || !aec->aec_handle) return;
    aec_state_t *st = (aec_state_t *)aec->aec_handle;
    free(st->w);
    free(st->x);
    free(st);
    aec->aec_handle = NULL;
}
