// codec_driver.c
#include "codec_driver.h"
#include "app_config.h"
#include "config_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "CODEC";

#ifdef USE_CODEC_ES8388

#include "driver/i2c.h"

#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_FREQ_HZ  100000
// ES8388 7-bit I2C address. CE/AD0 strap selects 0x10 (low) or 0x11 (high).
#define ES8388_ADDR         0x10

// --- ES8388 register map (subset used here) ---
#define ES8388_CONTROL1     0x00
#define ES8388_CONTROL2     0x01
#define ES8388_CHIPPOWER    0x02
#define ES8388_ADCPOWER     0x03
#define ES8388_DACPOWER     0x04
#define ES8388_ADCCONTROL1  0x09
#define ES8388_ADCCONTROL2  0x0A
#define ES8388_ADCCONTROL3  0x0B
#define ES8388_ADCCONTROL4  0x0C
#define ES8388_ADCCONTROL5  0x0D
#define ES8388_ADCCONTROL8  0x10
#define ES8388_ADCCONTROL9  0x11
#define ES8388_DACCONTROL1  0x17
#define ES8388_DACCONTROL2  0x18
#define ES8388_DACCONTROL3  0x19
#define ES8388_DACCONTROL21 0x2B
#define ES8388_DACCONTROL23 0x2D
#define ES8388_DACCONTROL24 0x2E   // LOUT1 volume
#define ES8388_DACCONTROL25 0x2F   // ROUT1 volume
#define ES8388_DACCONTROL26 0x30   // LOUT2 volume
#define ES8388_DACCONTROL27 0x31   // ROUT2 volume

static esp_err_t i2c_master_init(void) {
    hardware_settings_t hw;
    config_manager_load_hw(&hw);
    int sda = hw.pin_i2c_sda != -1 ? hw.pin_i2c_sda : CODEC_I2C_SDA_PIN;
    int scl = hw.pin_i2c_scl != -1 ? hw.pin_i2c_scl : CODEC_I2C_SCL_PIN;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t es_write(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg, data };
    return i2c_master_write_to_device(I2C_MASTER_NUM, ES8388_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

esp_err_t codec_init(void) {
    ESP_LOGI(TAG, "Initializing ES8388 over I2C (addr 0x%02X)...", ES8388_ADDR);
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master init failed: %d", ret);
        return ret;
    }

    // Standard ES8388 bring-up sequence (line-in mic -> ADC, DAC -> HP/line out).
    // Mirrors the widely-used ESP-ADF / AI-Thinker reference configuration.
    ret  = es_write(ES8388_CONTROL2,     0x50);
    ret |= es_write(ES8388_CHIPPOWER,    0x00); // power up all
    ret |= es_write(ES8388_CONTROL1,     0x12); // enable ref, soft ramp
    ret |= es_write(ES8388_DACPOWER,     0x00); // power up DAC + outputs
    // ADC path
    ret |= es_write(ES8388_ADCPOWER,     0x00);
    ret |= es_write(ES8388_ADCCONTROL1,  0x88); // PGA gain +24dB (mic)
    ret |= es_write(ES8388_ADCCONTROL2,  0x00); // LINSEL/RINSEL = LIN1/RIN1
    ret |= es_write(ES8388_ADCCONTROL3,  0x02);
    ret |= es_write(ES8388_ADCCONTROL4,  0x0C); // 16-bit, I2S format
    ret |= es_write(ES8388_ADCCONTROL5,  0x02); // MCLK/256 ratio
    ret |= es_write(ES8388_ADCCONTROL8,  0x00); // ADC L volume 0dB
    ret |= es_write(ES8388_ADCCONTROL9,  0x00); // ADC R volume 0dB
    // DAC path
    ret |= es_write(ES8388_DACCONTROL1,  0x18); // 16-bit, I2S format
    ret |= es_write(ES8388_DACCONTROL2,  0x02); // MCLK/256 ratio
    ret |= es_write(ES8388_DACCONTROL3,  0x00); // unmute DAC
    ret |= es_write(ES8388_DACCONTROL23, 0x00);
    // Route DAC to output mixer
    ret |= es_write(0x27, 0xB8);
    ret |= es_write(0x2A, 0xB8);
    // Output volumes (0..0x21). Start at a safe mid level.
    ret |= es_write(ES8388_DACCONTROL24, 0x1E);
    ret |= es_write(ES8388_DACCONTROL25, 0x1E);
    ret |= es_write(ES8388_DACCONTROL26, 0x1E);
    ret |= es_write(ES8388_DACCONTROL27, 0x1E);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 init sequence failed (check wiring / I2C address)");
        i2c_driver_delete(I2C_MASTER_NUM);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ES8388 initialized.");
    return ESP_OK;
}

esp_err_t codec_deinit(void) {
    return i2c_driver_delete(I2C_MASTER_NUM);
}

esp_err_t codec_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) volume_percent = 100;
    uint8_t reg = (uint8_t)((volume_percent * 0x21) / 100); // 0..33
    ESP_LOGI(TAG, "Volume %d%% (reg 0x%02X)", volume_percent, reg);
    esp_err_t r  = es_write(ES8388_DACCONTROL24, reg);
    r |= es_write(ES8388_DACCONTROL25, reg);
    r |= es_write(ES8388_DACCONTROL26, reg);
    r |= es_write(ES8388_DACCONTROL27, reg);
    return r;
}

esp_err_t codec_set_mic_gain(uint8_t gain_db) {
    // ADCCONTROL1 holds the L/R PGA gain in 3dB steps (0x00..0x88 = 0..+24dB).
    uint8_t step = gain_db / 3;
    if (step > 8) step = 8;
    uint8_t reg = (uint8_t)((step << 4) | step);
    ESP_LOGI(TAG, "Mic gain ~%ddB (reg 0x%02X)", gain_db, reg);
    return es_write(ES8388_ADCCONTROL1, reg);
}

#elif defined(USE_CODEC_INMP441_MAX98357A)

esp_err_t codec_init(void) {
    ESP_LOGI(TAG, "INMP441 + MAX98357A selected (pure I2S, no I2C control).");
    return ESP_OK;
}
esp_err_t codec_deinit(void) { return ESP_OK; }
esp_err_t codec_set_volume(uint8_t volume_percent) {
    (void)volume_percent;
    ESP_LOGW(TAG, "MAX98357A has no I2C volume; scale digitally if needed.");
    return ESP_OK;
}
esp_err_t codec_set_mic_gain(uint8_t gain_db) {
    (void)gain_db;
    ESP_LOGW(TAG, "INMP441 fixed gain.");
    return ESP_OK;
}

#else
#error "No audio codec selected in app_config.h (define USE_CODEC_ES8388 or USE_CODEC_INMP441_MAX98357A)"
#endif
