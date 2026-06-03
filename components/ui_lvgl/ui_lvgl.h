#ifndef UI_LVGL_H
#define UI_LVGL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_DIALER,
    SCREEN_CALLING,
    SCREEN_INCOMING
} ui_screen_t;

void ui_lvgl_init(void);
void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id);

#ifdef __cplusplus
}
#endif

#endif // UI_LVGL_H
