cmake_minimum_required(VERSION 3.10)
project(lvgl_simulator)

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

# Include directories
include_directories(
    .
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

# Pass command line options to compiler
if(SIM_THEME)
    target_compile_definitions(simulator PRIVATE SIM_THEME=${SIM_THEME})
endif()
if(SIM_ROUND)
    target_compile_definitions(simulator PRIVATE SIM_ROUND=${SIM_ROUND})
endif()

# Link libraries
target_link_libraries(simulator PRIVATE lvgl lvgl::examples lvgl::demos lv_drivers ${SDL2_LIBRARIES})
target_include_directories(simulator PRIVATE ${SDL2_INCLUDE_DIRS})
