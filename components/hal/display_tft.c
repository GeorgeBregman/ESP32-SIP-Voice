#include "display_tft.h"
#include "app_config.h"
#include "esp_log.h"

static const char *TAG = "TFT";

#if defined(USE_DISPLAY_ST7789) || defined(USE_DISPLAY_ILI9341)

// Include esp_lcd headers if we were doing a full implementation
// #include "esp_lcd_panel_io.h"
// #include "esp_lcd_panel_vendor.h"
// #include "esp_lcd_panel_ops.h"

void display_tft_init(void) {
#ifdef USE_DISPLAY_ST7789
    ESP_LOGI(TAG, "Initializing ST7789 TFT display...");
#elif defined(USE_DISPLAY_ILI9341)
    ESP_LOGI(TAG, "Initializing ILI9341 TFT display...");
#endif
    // Mock initialization for now. In a real app, setup SPI bus and esp_lcd panel.
}

void display_tft_draw_dialer(const char* number) {
    ESP_LOGI(TAG, "TFT UI: Dialer [ %s ]", number ? number : "");
}

void display_tft_draw_calling(const char* target) {
    ESP_LOGI(TAG, "TFT UI: Calling %s...", target ? target : "Unknown");
}

void display_tft_draw_incall(const char* target, uint32_t duration_sec) {
    ESP_LOGI(TAG, "TFT UI: In Call with %s [%02ld:%02ld]", 
             target ? target : "Unknown", duration_sec / 60, duration_sec % 60);
}

#else

void display_tft_init(void) { }
void display_tft_draw_dialer(const char* number) { }
void display_tft_draw_calling(const char* target) { }
void display_tft_draw_incall(const char* target, uint32_t duration_sec) { }

#endif // Display Selection
