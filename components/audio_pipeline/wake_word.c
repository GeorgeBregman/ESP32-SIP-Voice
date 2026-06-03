// wake_word.c
//
// Offline wake-word detection using esp-sr WakeNet (API v2.x model loader).
// Models are stored in a `model` flash partition and loaded at runtime via
// esp_srmodel_init(); select the keyword with WAKE_WORD_MODEL (e.g. "computer").
#include "wake_word.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>

#if USE_WAKE_WORD

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

static const char *TAG = "WAKE_WORD";

static const esp_wn_iface_t *s_wakenet = NULL;
static model_iface_data_t   *s_model_data = NULL;
static srmodel_list_t       *s_models = NULL;
static int                   s_chunksize = 0;

bool wake_word_init(void) {
    s_models = esp_srmodel_init("model");   // "model" = partition label
    if (!s_models) {
        ESP_LOGE(TAG, "esp_srmodel_init failed (no 'model' partition flashed?)");
        return false;
    }

    // Pick a WakeNet model, preferring the configured keyword if present.
    char *name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, WAKE_WORD_MODEL);
    if (!name) name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (!name) {
        ESP_LOGE(TAG, "No WakeNet model found in partition");
        return false;
    }

    s_wakenet = esp_wn_handle_from_name(name);
    if (!s_wakenet) {
        ESP_LOGE(TAG, "esp_wn_handle_from_name(%s) failed", name);
        return false;
    }

    s_model_data = s_wakenet->create(name, DET_MODE_90);
    if (!s_model_data) {
        ESP_LOGE(TAG, "WakeNet create(%s) failed", name);
        return false;
    }
    s_chunksize = s_wakenet->get_samp_chunksize(s_model_data);
    ESP_LOGI(TAG, "WakeNet ready: model=%s chunksize=%d", name, s_chunksize);
    return true;
}

bool wake_word_feed(int16_t *data, int len_bytes) {
    if (!s_wakenet || !s_model_data || s_chunksize <= 0) return false;

    int num_samples = len_bytes / (int)sizeof(int16_t);
    int offset = 0;
    while (num_samples - offset >= s_chunksize) {
        wakenet_state_t st = s_wakenet->detect(s_model_data, &data[offset]);
        if (st == WAKENET_DETECTED) {
            ESP_LOGI(TAG, ">>> WAKE WORD DETECTED <<<");
            return true;
        }
        offset += s_chunksize;
    }
    return false;
}

#else // !USE_WAKE_WORD

bool wake_word_init(void) { return false; }
bool wake_word_feed(int16_t *data, int len_bytes) { (void)data; (void)len_bytes; return false; }

#endif
