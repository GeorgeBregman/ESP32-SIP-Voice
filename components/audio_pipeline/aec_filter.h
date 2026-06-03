#ifndef AEC_FILTER_H
#define AEC_FILTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *aec_handle;
} aec_filter_t;

// Initialize Acoustic Echo Cancellation
bool aec_filter_init(aec_filter_t *aec, int sample_rate, int frame_size);

// Process a frame of audio.
// mic_in: Audio from the local microphone
// ref_in: Audio currently playing out of the local speaker (the reference signal)
// out: Cleaned audio to send over the network
// Returns true on success.
bool aec_filter_process(aec_filter_t *aec, const int16_t *mic_in, const int16_t *ref_in, int16_t *out);

// Deinitialize
void aec_filter_deinit(aec_filter_t *aec);

#ifdef __cplusplus
}
#endif

#endif /* AEC_FILTER_H */
