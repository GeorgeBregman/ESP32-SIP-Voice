#include "lvgl.h"
#include "sdl/sdl.h"
#include "../components/ui_lvgl/ui_lvgl.h"
#include <unistd.h>
#include <stdio.h>

// Tracks what the UI is currently showing so the demo can auto-re-arm.
static ui_screen_t g_state = SCREEN_IDLE;

// The on-screen buttons call these (wired via ui_lvgl_set_action_cb), so the
// simulator is actually interactive: click green = answer, red = hang up.
static void sim_answer(void) {
    g_state = SCREEN_ACTIVE;
    ui_lvgl_switch_screen(SCREEN_ACTIVE, "Front Door");
    printf("[SIM] Answered -> ACTIVE\n");
}
static void sim_hangup(void) {
    g_state = SCREEN_IDLE;
    ui_lvgl_switch_screen(SCREEN_IDLE, NULL);
    printf("[SIM] Hung up -> IDLE (clock)\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    lv_init();
    sdl_init();

    // SDL display
    static lv_disp_draw_buf_t disp_buf;
    static lv_color_t buf[SDL_HOR_RES * SDL_VER_RES];
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, SDL_HOR_RES * SDL_VER_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = SDL_HOR_RES;
    disp_drv.ver_res = SDL_VER_RES;
    lv_disp_drv_register(&disp_drv);

    // Mouse acts as the touchscreen
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    printf("Starting LVGL UI Simulator (%dx%d)...\n", SDL_HOR_RES, SDL_VER_RES);
    printf("Click the green/red buttons to answer / hang up.\n");

    ui_lvgl_init();
    ui_lvgl_set_action_cb(sim_answer, sim_hangup);
    ui_lvgl_set_registered(true);

    // Begin the showcase on an incoming intercom call.
    g_state = SCREEN_INCOMING;
    ui_lvgl_switch_screen(SCREEN_INCOMING, "Front Door");

    // Main loop: ~5 ms tick. When the user ends a call and the screen has been
    // idle for a while, re-arm an incoming call so the demo keeps cycling.
    int idle_ms = 0;
    while (1) {
        lv_timer_handler();
        usleep(5000);

        if (g_state == SCREEN_IDLE) {
            idle_ms += 5;
            if (idle_ms >= 6000) {            // 6 s on the clock face
                idle_ms = 0;
                g_state = SCREEN_INCOMING;
                ui_lvgl_switch_screen(SCREEN_INCOMING, "Front Door");
                printf("[SIM] New incoming call\n");
            }
        } else {
            idle_ms = 0;
        }
    }
    return 0;
}
