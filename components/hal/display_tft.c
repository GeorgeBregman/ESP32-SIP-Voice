#include "display_tft.h"
#include "app_config.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

// If we use lvgl:
// #include "lvgl.h"

static const char *TAG = "TFT";

#ifdef USE_DISPLAY_GC9A01

// Try to include the component header
// Note: You must add espressif/esp_lcd_gc9a01 to idf_component.yml
#if __has_include("esp_lcd_gc9a01.h")
#include "esp_lcd_gc9a01.h"
#endif

esp_lcd_panel_handle_t panel_handle = NULL;

void display_tft_init(void) {
    ESP_LOGI(TAG, "Initializing GC9A01 TFT display...");

    hardware_settings_t hw;
    config_manager_load_hw(&hw);
    
    // Fallbacks if not set in NVS
    int mosi = hw.pin_spi_mosi != -1 ? hw.pin_spi_mosi : 23;
    int clk = hw.pin_spi_clk != -1 ? hw.pin_spi_clk : 18;
    int cs = hw.pin_spi_cs != -1 ? hw.pin_spi_cs : 5;
    int dc = hw.pin_spi_dc != -1 ? hw.pin_spi_dc : 2;
    int rst = hw.pin_spi_rst != -1 ? hw.pin_spi_rst : 4;
    
    spi_bus_config_t buscfg = {
        .sclk_io_num = clk,
        .mosi_io_num = mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 240 * 2 + 8
    };
    // Ignore error if already initialized by something else
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
    
#if __has_include("esp_lcd_gc9a01.h")
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
#else
    ESP_LOGE(TAG, "esp_lcd_gc9a01 component not found! Cannot initialize panel.");
    return;
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    ESP_LOGI(TAG, "GC9A01 initialized.");
}

void display_tft_draw_dialer(const char* number) { }
void display_tft_draw_calling(const char* target) { }
void display_tft_draw_incall(const char* target, uint32_t duration_sec) { }

#else

void display_tft_init(void) { }
void display_tft_draw_dialer(const char* number) { }
void display_tft_draw_calling(const char* target) { }
void display_tft_draw_incall(const char* target, uint32_t duration_sec) { }

#endif // Display Selection
