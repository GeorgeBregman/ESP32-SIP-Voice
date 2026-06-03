#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include <stdint.h>
#include <stdbool.h>

void display_tft_init(void);
void display_tft_draw_dialer(const char* number);
void display_tft_draw_calling(const char* target);
void display_tft_draw_incall(const char* target, uint32_t duration_sec);

// Actual panel resolution (pixels) and physical geometry, used by the LVGL UI.
void display_tft_get_resolution(int *w, int *h);
bool display_tft_is_round(void);

#endif // DISPLAY_TFT_H
