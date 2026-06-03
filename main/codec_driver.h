// codec_driver.h
#ifndef CODEC_DRIVER_H
#define CODEC_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t codec_init(void);
esp_err_t codec_deinit(void);
esp_err_t codec_set_volume(uint8_t volume_percent); // 0-100
esp_err_t codec_set_mic_gain(uint8_t gain_db);      // approximate dB

#ifdef __cplusplus
}
#endif

#endif // CODEC_DRIVER_H
