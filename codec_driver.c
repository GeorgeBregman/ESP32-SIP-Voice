// codec_driver.h
#ifndef CODEC_DRIVER_H
#define CODEC_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t codec_init(void);
esp_err_t codec_deinit(void);
esp_err_t codec_set_volume(uint8_t volume_percent); // 0-100
esp_err_t codec_set_mic_gain(uint8_t gain_db);     // Gain in dB (check codec datasheet)
// Add other codec-specific functions if needed (mute, power modes, etc.)

#endif // CODEC_DRIVER_H


// codec_driver.c
#include "codec_driver.h"
#include "app_config.h" // For I2C pins etc.
#include "esp_log.h"
// Include specific headers for your codec, potentially I2C driver
#include "driver/i2c.h"

static const char* TAG = "CODEC";

// --- Placeholder Implementation for an I2C Codec (e.g., ES8388) ---
// *** Replace with actual driver code for your chosen codec ***

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

// ... Add functions to read registers if needed ...

esp_err_t codec_init(void) {
    ESP_LOGI(TAG, "Initializing Codec...");
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C Master Init Failed: %d", ret);
         return ret;
    }
    ESP_LOGI(TAG, "I2C Master Initialized.");

    // --- Send Initialization Sequence specific to your Codec ---
    // Example for ES8388 (Sequence may vary based on datasheet/board)
    ESP_LOGI(TAG, "Sending Codec Init Sequence...");
    ret |= codec_write_reg(0x00, 0x00); // Reset Register
    vTaskDelay(pdMS_TO_TICKS(50)); // Short delay after reset
    ret |= codec_write_reg(0x04, 0x0C); // Clock Manager Control 1 (MCLK / 2)
    ret |= codec_write_reg(0x01, 0x50); // Clock Manager Control 2 (Enable Clocks)
    ret |= codec_write_reg(0x02, 0x00); // System Control 1 (Normal Operation)
    ret |= codec_write_reg(0x03, 0x10); // System Control 2 (I2S Mode, 16bit) - Adjust if needed!
    ret |= codec_write_reg(0x1A, 0x0A); // ADC Control 1 (Enable ADC)
    ret |= codec_write_reg(0x1B, 0x00); // ADC Control 2 (PGA Gain = 0dB) - Adjust MIC Gain here or later
    ret |= codec_write_reg(0x1C, 0x6A); // ADC Control 3
    // ... Add more configuration registers for DAC, Mic Bias, Power Management ...
     ret |= codec_write_reg(0x27, 0x00); // DAC Control 1 (Enable DAC)
     ret |= codec_write_reg(0x2A, 0x30); // DAC Control 4 (Initial Volume?)

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Codec Init Sequence Failed!");
         i2c_driver_delete(I2C_MASTER_NUM);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Codec Initialized Successfully.");
    return ESP_OK;
}

esp_err_t codec_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Codec...");
    // Send power-down sequence if necessary
    // codec_write_reg( ... );
    esp_err_t ret = i2c_driver_delete(I2C_MASTER_NUM);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C driver delete failed: %d", ret);
     } else {
          ESP_LOGI(TAG, "Codec Deinitialized.");
     }
     return ret;
}

esp_err_t codec_set_volume(uint8_t volume_percent) {
     if (volume_percent > 100) volume_percent = 100;
     // Map 0-100% to codec register values (e.g., 0-255 or specific dB steps)
     // Example for ES8388: Volume might range from -96dB to 0dB. Check datasheet.
     // This is a placeholder mapping.
     uint8_t reg_val = (uint8_t)((volume_percent / 100.0f) * 33.0f); // Map to DAC Control regs? Check datasheet.
     ESP_LOGI(TAG, "Setting volume to %d%% (Reg: 0x%02X)", volume_percent, reg_val);
     // Write to appropriate DAC volume register(s), e.g., 0x2A, 0x2B for ES8388
     // return codec_write_reg(0x2A, reg_val);
     return ESP_OK; // Placeholder
}

esp_err_t codec_set_mic_gain(uint8_t gain_db) {
     // Map dB value (e.g., 0, 3, 6 ... 24dB) to codec register value
     // Example for ES8388: PGA gain in ADC Control 2 (0x1B)
     uint8_t reg_val = 0x00; // Default 0dB
     if (gain_db >= 24) reg_val = 0x08;
     else if (gain_db >= 21) reg_val = 0x07;
     // ... map other gain steps ...
     ESP_LOGI(TAG, "Setting Mic Gain to %ddB (Reg: 0x%02X)", gain_db, reg_val);
     return codec_write_reg(0x1B, reg_val); // Write to PGA gain register
}
