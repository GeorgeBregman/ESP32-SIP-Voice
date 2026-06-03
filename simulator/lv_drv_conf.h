#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

// SDL Configuration
#define USE_SDL 1

// SDL Display settings
#if USE_SDL
#  define SDL_HOR_RES     240
#  define SDL_VER_RES     320
#  define SDL_ZOOM        1
#  define SDL_INCLUDE_PATH <SDL2/SDL.h>
#endif

// We don't need these for the simulator
#define USE_X11 0
#define USE_WAYLAND 0

#endif /*LV_DRV_CONF_H*/
