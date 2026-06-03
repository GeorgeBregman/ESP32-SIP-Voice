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

#endif /*LV_CONF_H*/
