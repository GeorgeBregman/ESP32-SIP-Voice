#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include <stdint.h>

void display_tft_init(void);
void display_tft_draw_dialer(const char* number);
void display_tft_draw_calling(const char* target);
void display_tft_draw_incall(const char* target, uint32_t duration_sec);

#endif // DISPLAY_TFT_H
