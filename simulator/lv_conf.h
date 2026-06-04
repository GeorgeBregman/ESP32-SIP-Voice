#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// LVGL Configuration for Simulator
#define LV_COLOR_DEPTH 32
#define LV_COLOR_16_SWAP 0

// Memory allocation
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128U * 1024U)

// Tick configuration
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <SDL2/SDL.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (SDL_GetTicks())

// Enable features we might need
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_USE_FONT_COMPRESSED 1

// Fonts used by the UI themes (clock + labels). Must match what ui_lvgl.c's
// FONT_XL/L/M macros look for, otherwise everything falls back to 14px and the
// big clock renders tiny. (On-device these come from sdkconfig.defaults.)
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /*LV_CONF_H*/
