cmake_minimum_required(VERSION 3.10)
project(lvgl_simulator C)

# IMPORTANT: these must be set BEFORE the LVGL / lv_drivers libraries are added,
# so that *their* sources also see them:
#  - LV_CONF_INCLUDE_SIMPLE / LV_DRV_CONF_INCLUDE_SIMPLE make LVGL & lv_drivers
#    resolve the config via a plain  #include "lv_conf.h"  /  "lv_drv_conf.h".
#  - the include dir below puts this folder's lv_conf.h / lv_drv_conf.h on the
#    path for every target (the libraries included via FetchContent included).
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE LV_DRV_CONF_INCLUDE_SIMPLE)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Theme / geometry selectors. Made GLOBAL (and set before the libraries are
# added) so lv_drv_conf.h — compiled as part of lv_drivers — also sees SIM_ROUND
# and can pick a square vs rectangular SDL window.
#   cmake .. -DSIM_THEME=2 -DSIM_ROUND=1
if(DEFINED SIM_THEME)
    add_compile_definitions(SIM_THEME=${SIM_THEME})
endif()
if(DEFINED SIM_ROUND)
    add_compile_definitions(SIM_ROUND=${SIM_ROUND})
endif()

# Fetch LVGL and Drivers
include(FetchContent)

FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG        v8.3.11
)

FetchContent_Declare(
  lv_drivers
  GIT_REPOSITORY https://github.com/lvgl/lv_drivers.git
  GIT_TAG        v8.3.0
)

FetchContent_MakeAvailable(lvgl lv_drivers)

# Find SDL2
find_package(SDL2 REQUIRED)

# Include directories for our own sources
include_directories(
    ${lvgl_SOURCE_DIR}
    ${lv_drivers_SOURCE_DIR}
    ../components/ui_lvgl
)

# Add our UI source files
set(SOURCES
    main.c
    ../components/ui_lvgl/ui_lvgl.c
)

add_executable(simulator ${SOURCES})

# Link libraries. (Only lvgl + lv_drivers + SDL2 — the lvgl::examples /
# lvgl::demos targets only exist when LVGL is built with examples/demos.)
target_link_libraries(simulator PRIVATE lvgl lv_drivers ${SDL2_LIBRARIES})
target_include_directories(simulator PRIVATE ${SDL2_INCLUDE_DIRS})
