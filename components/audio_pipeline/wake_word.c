#include "wake_word.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>

#if USE_WAKE_WORD

#include "esp_wn_iface.h"
#include "esp_wn_models.h"

static const char *TAG = "WAKE_WORD";
static const esp_wn_iface_t *wakenet_model = NULL;
static model_iface_data_t *wakenet_data = NULL;

bool wake_word_init(void) {
    wakenet_model = &WAKENET_MODEL;
    if (wakenet_model == NULL) {
        ESP_LOGE(TAG, "Failed to load WakeNet model interface");
        return false;
    }

    wakenet_data = wakenet_model->create(WAKE_WORD_MODEL, DET_MODE_90);
    if (wakenet_data == NULL) {
        ESP_LOGE(TAG, "Failed to create WakeNet instance for model %s", WAKE_WORD_MODEL);
        return false;
    }

    ESP_LOGI(TAG, "Wake Word initialized with model: %s", WAKE_WORD_MODEL);
    
    // Set detection threshold if supported by model (default is usually fine)
    // wakenet_model->set_det_threshold(wakenet_data, 0.5);
    
    return true;
}

bool wake_word_feed(int16_t *data, int len) {
    if (!wakenet_model || !wakenet_data) return false;

    // WakeNet typically expects 16kHz, 1 channel, 16-bit PCM.
    // Length per chunk is usually wakenet_model->get_samp_chunksize(wakenet_data)
    int chunksize = wakenet_model->get_samp_chunksize(wakenet_data);
    
    // For simplicity, we assume 'len' matches the chunksize * sizeof(int16_t)
    // In a real robust implementation, a ring buffer would be used here to strictly feed 'chunksize' samples.
    
    int num_samples = len / sizeof(int16_t);
    int offset = 0;
    
    while (num_samples - offset >= chunksize) {
        int result = wakenet_model->detect(wakenet_data, &data[offset]);
        if (result == 1) { // 1 means wake word detected
            ESP_LOGI(TAG, ">>> WAKE WORD DETECTED <<<");
            return true;
        }
        offset += chunksize;
    }

    return false;
}

#else

bool wake_word_init(void) { return false; }
bool wake_word_feed(int16_t *data, int len) { return false; }

#endif
