#ifndef UI_LVGL_H
#define UI_LVGL_H

#include <stdint.h>
#include <stdbool.h>

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
void ui_lvgl_update_energy(uint8_t energy_level);

// LVGL is not thread-safe: any lv_* call made from a task other than the
// internal LVGL task must be wrapped in lock()/unlock(). timeout_ms < 0 waits
// forever. Returns true if the lock was acquired.
bool ui_lvgl_lock(int timeout_ms);
void ui_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif

#endif // UI_LVGL_H
