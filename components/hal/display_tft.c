#include "display_tft.h"
#include "app_config.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "TFT";

// Panel handle is referenced by the LVGL component (extern). It must exist for
// EVERY display selection so the firmware links regardless of the chosen panel.
esp_lcd_panel_handle_t panel_handle = NULL;

// --- Resolution / geometry, overridable from app_config.h ---
#if defined(USE_DISPLAY_GC9A01)
  #ifndef TFT_H_RES
    #define TFT_H_RES 240
  #endif
  #ifndef TFT_V_RES
    #define TFT_V_RES 240
  #endif
  #define TFT_IS_ROUND 1
#elif defined(USE_DISPLAY_ILI9341)
  #ifndef TFT_H_RES
    #define TFT_H_RES 240
  #endif
  #ifndef TFT_V_RES
    #define TFT_V_RES 320
  #endif
  #define TFT_IS_ROUND 0
#else // USE_DISPLAY_ST7789 (default) — 1.3"/1.54" square by default; override for 2.x" panels
  #ifndef TFT_H_RES
    #define TFT_H_RES 240
  #endif
  #ifndef TFT_V_RES
    #define TFT_V_RES 240
  #endif
  #define TFT_IS_ROUND 0
#endif

#if defined(USE_DISPLAY_ILI9341) && __has_include("esp_lcd_ili9341.h")
#include "esp_lcd_ili9341.h"
#endif
#if defined(USE_DISPLAY_GC9A01) && __has_include("esp_lcd_gc9a01.h")
#include "esp_lcd_gc9a01.h"
#endif

void display_tft_get_resolution(int *w, int *h) {
    if (w) *w = TFT_H_RES;
    if (h) *h = TFT_V_RES;
}

bool display_tft_is_round(void) {
    return TFT_IS_ROUND ? true : false;
}

void display_tft_init(void) {
    ESP_LOGI(TAG, "Initializing TFT display (%dx%d)...", TFT_H_RES, TFT_V_RES);

    hardware_settings_t hw;
    config_manager_load_hw(&hw);

    int mosi = hw.pin_spi_mosi != -1 ? hw.pin_spi_mosi : TFT_MOSI;
    int clk  = hw.pin_spi_clk  != -1 ? hw.pin_spi_clk  : TFT_SCLK;
    int cs   = hw.pin_tft_cs   != -1 ? hw.pin_tft_cs   : TFT_CS;
    int dc   = hw.pin_tft_dc   != -1 ? hw.pin_tft_dc   : TFT_DC;
    int rst  = hw.pin_tft_rst  != -1 ? hw.pin_tft_rst  : TFT_RST;

    spi_bus_config_t buscfg = {
        .sclk_io_num = clk,
        .mosi_io_num = mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_H_RES * 80 * sizeof(uint16_t) + 8,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = dc,
        .cs_gpio_num = cs,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = rst,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };

#if defined(USE_DISPLAY_GC9A01)
  #if __has_include("esp_lcd_gc9a01.h")
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
  #else
    ESP_LOGE(TAG, "esp_lcd_gc9a01 component missing — add espressif/esp_lcd_gc9a01 to idf_component.yml");
    return;
  #endif
#elif defined(USE_DISPLAY_ILI9341)
  #if __has_include("esp_lcd_ili9341.h")
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
  #else
    ESP_LOGE(TAG, "esp_lcd_ili9341 component missing — add espressif/esp_lcd_ili9341 to idf_component.yml");
    return;
  #endif
#else // ST7789 is built into esp_lcd; no extra component required
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // ST7789/GC9A01 panels are typically colour-inverted in this wiring.
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, true, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_LOGI(TAG, "TFT panel initialized.");
}

void display_tft_draw_dialer(const char* number) { (void)number; }
void display_tft_draw_calling(const char* target) { (void)target; }
void display_tft_draw_incall(const char* target, uint32_t duration_sec) { (void)target; (void)duration_sec; }
