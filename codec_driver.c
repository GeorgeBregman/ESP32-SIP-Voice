// codec_driver.h
#ifndef CODEC_DRIVER_H
#define CODEC_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t codec_init(void);
esp_err_t codec_deinit(void);
esp_err_t codec_set_volume(uint8_t volume_percent); // 0-100
esp_err_t codec_set_mic_gain(uint8_t gain_db);     // Gain in dB (check codec datasheet)

#endif // CODEC_DRIVER_H


// codec_driver.c
#include "codec_driver.h"
#include "app_config.h" // For I2C pins etc.
#include "esp_log.h"

static const char* TAG = "CODEC";

#ifdef USE_CODEC_ES8388

#include "driver/i2c.h"

#define I2C_MASTER_NUM I2C_NUM_0 // Choose I2C peripheral
#define I2C_MASTER_FREQ_HZ 100000 // Typical I2C speed
#define CODEC_I2C_ADDRESS 0x18    // Example I2C address for ES8388 (check datasheet/schematic)

static esp_err_t i2c_master_init() {
     i2c_config_t conf = {
         .mode = I2C_MODE_MASTER,
         .sda_io_num = CODEC_I2C_SDA_PIN,
         .scl_io_num = CODEC_I2C_SCL_PIN,
         .sda_pullup_en = GPIO_PULLUP_ENABLE,
         .scl_pullup_en = GPIO_PULLUP_ENABLE,
         .master.clk_speed = I2C_MASTER_FREQ_HZ,
     };
     esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
     if (err != ESP_OK) return err;
     return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t codec_write_reg(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, CODEC_I2C_ADDRESS, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}

esp_err_t codec_init(void) {
    ESP_LOGI(TAG, "Initializing ES8388 Codec over I2C...");
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C Master Init Failed: %d", ret);
         return ret;
    }
    ESP_LOGI(TAG, "I2C Master Initialized.");

    // Sequence for ES8388
    ESP_LOGI(TAG, "Sending ES8388 Init Sequence...");
    ret |= codec_write_reg(0x00, 0x00); // Reset Register
    vTaskDelay(pdMS_TO_TICKS(50));
    ret |= codec_write_reg(0x04, 0x0C); // Clock Manager Control 1
    ret |= codec_write_reg(0x01, 0x50); // Clock Manager Control 2
    ret |= codec_write_reg(0x02, 0x00); // System Control 1
    ret |= codec_write_reg(0x03, 0x10); // System Control 2
    ret |= codec_write_reg(0x1A, 0x0A); // ADC Control 1
    ret |= codec_write_reg(0x1B, 0x00); // ADC Control 2
    ret |= codec_write_reg(0x1C, 0x6A); // ADC Control 3
    ret |= codec_write_reg(0x27, 0x00); // DAC Control 1
    ret |= codec_write_reg(0x2A, 0x30); // DAC Control 4

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 Init Sequence Failed!");
        i2c_driver_delete(I2C_MASTER_NUM);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ES8388 Initialized Successfully.");
    return ESP_OK;
}

esp_err_t codec_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing ES8388 Codec...");
    esp_err_t ret = i2c_driver_delete(I2C_MASTER_NUM);
    return ret;
}

esp_err_t codec_set_volume(uint8_t volume_percent) {
     if (volume_percent > 100) volume_percent = 100;
     uint8_t reg_val = (uint8_t)((volume_percent / 100.0f) * 33.0f);
     ESP_LOGI(TAG, "Setting volume to %d%% (Reg: 0x%02X)", volume_percent, reg_val);
     return codec_write_reg(0x2A, reg_val);
}

esp_err_t codec_set_mic_gain(uint8_t gain_db) {
     uint8_t reg_val = 0x00; // Default 0dB
     if (gain_db >= 24) reg_val = 0x08;
     else if (gain_db >= 21) reg_val = 0x07;
     ESP_LOGI(TAG, "Setting Mic Gain to %ddB (Reg: 0x%02X)", gain_db, reg_val);
     return codec_write_reg(0x1B, reg_val);
}

#elif defined(USE_CODEC_INMP441_MAX98357A)

esp_err_t codec_init(void) {
    ESP_LOGI(TAG, "INMP441 + MAX98357A Codec selected. No I2C initialization needed.");
    // No specific initialization required since they communicate directly over I2S.
    return ESP_OK;
}

esp_err_t codec_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing INMP441 + MAX98357A Codec.");
    return ESP_OK;
}

esp_err_t codec_set_volume(uint8_t volume_percent) {
    ESP_LOGW(TAG, "MAX98357A does not support I2C volume control. Adjust via I2S digital scaling if needed.");
    return ESP_OK;
}

esp_err_t codec_set_mic_gain(uint8_t gain_db) {
    ESP_LOGW(TAG, "INMP441 does not support I2C gain control. Fixed gain sensor.");
    return ESP_OK;
}

#else
#error "No audio codec selected in app_config.h"
#endif
