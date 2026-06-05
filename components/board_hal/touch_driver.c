#include "touch_driver.h"
#include "app_config.h"
#include "display_tft.h"
#include "esp_log.h"

static const char *TAG = "TOUCH";

#ifdef USE_TOUCH_XPT2046

#include "driver/spi_master.h"
#include "driver/gpio.h"

// XPT2046 control bytes (12-bit, differential): X = 0x90, Y = 0xD0.
#define XPT_CMD_X  0x90
#define XPT_CMD_Y  0xD0

// Raw ADC calibration bounds. These ARE panel-specific — tune for your unit
// (override in app_config.h). Defaults are typical for 2.4"/2.8" modules.
#ifndef TOUCH_X_MIN
#define TOUCH_X_MIN 200
#endif
#ifndef TOUCH_X_MAX
#define TOUCH_X_MAX 3900
#endif
#ifndef TOUCH_Y_MIN
#define TOUCH_Y_MIN 200
#endif
#ifndef TOUCH_Y_MAX
#define TOUCH_Y_MAX 3900
#endif
// Set to 1 in app_config.h if X/Y are swapped or mirrored for your rotation.
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X 0
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y 0
#endif

static spi_device_handle_t s_touch = NULL;
static int s_w = 240, s_h = 240;

static uint16_t xpt_read(uint8_t cmd) {
    if (!s_touch) return 0;
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    spi_transaction_t t = {
        .length = 3 * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_polling_transmit(s_touch, &t) != ESP_OK) return 0;
    // 12-bit result is in bits [14:3] of the last two bytes.
    return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3) & 0x0FFF;
}

void touch_driver_init(void) {
    ESP_LOGI(TAG, "Initializing XPT2046 (CS %d, IRQ %d)", TOUCH_CS, TOUCH_IRQ);
    display_tft_get_resolution(&s_w, &s_h);

    // IRQ pin as input (low when the panel is touched).
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << TOUCH_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    // Touch shares the TFT SPI bus (SPI2_HOST), which display_tft_init() already
    // initialized. Add the controller as a slow (~2 MHz) device.
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = TOUCH_CS,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &devcfg, &s_touch) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add XPT2046 to SPI bus");
        s_touch = NULL;
    }
}

bool touch_driver_read(int16_t *x, int16_t *y) {
    if (!s_touch) return false;
    if (gpio_get_level(TOUCH_IRQ) != 0) return false; // not pressed

    // Average a few samples for stability.
    uint32_t rx = 0, ry = 0;
    const int N = 4;
    for (int i = 0; i < N; i++) {
        rx += xpt_read(XPT_CMD_X);
        ry += xpt_read(XPT_CMD_Y);
    }
    rx /= N; ry /= N;
    if (rx < 32 || ry < 32) return false; // bogus / released mid-read

    int px = (int)((rx - TOUCH_X_MIN) * (long)s_w / (TOUCH_X_MAX - TOUCH_X_MIN));
    int py = (int)((ry - TOUCH_Y_MIN) * (long)s_h / (TOUCH_Y_MAX - TOUCH_Y_MIN));
#if TOUCH_INVERT_X
    px = s_w - px;
#endif
#if TOUCH_INVERT_Y
    py = s_h - py;
#endif
#if TOUCH_SWAP_XY
    int tmp = px; px = py; py = tmp;
#endif
    if (px < 0) px = 0; else if (px >= s_w) px = s_w - 1;
    if (py < 0) py = 0; else if (py >= s_h) py = s_h - 1;

    if (x) *x = (int16_t)px;
    if (y) *y = (int16_t)py;
    return true;
}

#else // no touch controller

void touch_driver_init(void) { ESP_LOGI(TAG, "No touch controller configured."); }
bool touch_driver_read(int16_t *x, int16_t *y) { (void)x; (void)y; return false; }

#endif
