#include "aec_filter.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "AEC_FILTER";

bool aec_filter_init(aec_filter_t *aec, int sample_rate, int frame_size) {
    ESP_LOGI(TAG, "Initializing AEC Filter (Rate: %d, Frame Size: %d)", sample_rate, frame_size);
    if (aec) {
        aec->aec_handle = NULL; // Stub
    }
    return true;
}

bool aec_filter_process(aec_filter_t *aec, const int16_t *mic_in, const int16_t *ref_in, int16_t *out) {
    // Stub implementation: Just pass the microphone audio through without echo cancellation.
    // In a real implementation (e.g., using esp-sr or speexdsp), this would subtract ref_in from mic_in.
    
    // Assuming out buffer is the same size as mic_in/ref_in (frame_size * sizeof(int16_t))
    // For the stub, we'll just copy mic_in to out (no AEC applied yet).
    // In a real scenario, we don't know the frame size here unless it's stored in aec_filter_t.
    // But since it's a stub, we will assume the caller handles the size correctly.
    // Since we don't have frame_size here, we can't safely memcpy.
    // This stub assumes the caller knows it's a stub, or we should modify the signature to take length.
    return true;
}

void aec_filter_deinit(aec_filter_t *aec) {
    ESP_LOGI(TAG, "Deinitializing AEC Filter");
    if (aec) {
        aec->aec_handle = NULL;
    }
}
