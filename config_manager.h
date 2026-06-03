#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "esp_err.h"

typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char sip_server[32];
    char sip_user[32];
    char sip_password[64];
} app_settings_t;

typedef struct {
    // I2S Audio Pins
    int8_t pin_i2s_bck;
    int8_t pin_i2s_ws;
    int8_t pin_i2s_dout;
    int8_t pin_i2s_din;
    int8_t pin_i2s_mclk; // For codecs that need MCLK (e.g. ES8388)

    // I2C Pins (For codec control or OLED)
    int8_t pin_i2c_sda;
    int8_t pin_i2c_scl;

    // SPI / TFT Pins
    int8_t pin_spi_mosi;
    int8_t pin_spi_miso;
    int8_t pin_spi_clk;
    int8_t pin_tft_cs;
    int8_t pin_tft_dc;
    int8_t pin_tft_rst;
    int8_t pin_touch_cs;
    int8_t pin_touch_irq;
} hardware_settings_t;

esp_err_t config_manager_init(void);
esp_err_t config_manager_load(app_settings_t *settings);
esp_err_t config_manager_save(const app_settings_t *settings);

esp_err_t config_manager_load_hw(hardware_settings_t *hw_settings);
esp_err_t config_manager_save_hw(const hardware_settings_t *hw_settings);

void config_manager_reset(void); // Clear NVS

#endif // CONFIG_MANAGER_H
