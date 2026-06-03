#include "lvgl.h"
#include "sdl/sdl.h"
#include "../components/ui_lvgl/ui_lvgl.h"
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
    // Initialize LVGL
    lv_init();

    // Initialize SDL driver
    sdl_init();

    // Create a display buffer
    static lv_disp_draw_buf_t disp_buf;
    static lv_color_t buf[SDL_HOR_RES * SDL_VER_RES];
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, SDL_HOR_RES * SDL_VER_RES);

    // Initialize display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = SDL_HOR_RES;
    disp_drv.ver_res = SDL_VER_RES;
    lv_disp_drv_register(&disp_drv);

    // Initialize input device driver (Mouse acting as touch)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    // Initialize our Custom UI
    printf("Starting LVGL UI Simulator...\n");
    ui_lvgl_init();
    ui_lvgl_switch_screen(SCREEN_DIALER, NULL);

    // LVGL main loop
    while(1) {
        lv_timer_handler();
        usleep(5000); // Sleep for 5ms
    }

    return 0;
}
