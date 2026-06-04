#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

// SDL Configuration
#define USE_SDL 1

// SDL Display settings.
// SIM_ROUND=1 (a circular GC9A01-style panel) uses a SQUARE window so the
// Smart-Speaker ring stays a circle; otherwise a 240x320 rectangle (ST7789).
#if USE_SDL
#  if defined(SIM_ROUND) && (SIM_ROUND == 1)
#    define SDL_HOR_RES   240
#    define SDL_VER_RES   240
#  else
#    define SDL_HOR_RES   240
#    define SDL_VER_RES   320
#  endif
#  define SDL_ZOOM        2
#  define SDL_INCLUDE_PATH <SDL2/SDL.h>
#endif

// We don't need these for the simulator
#define USE_X11 0
#define USE_WAYLAND 0

#endif /*LV_DRV_CONF_H*/
