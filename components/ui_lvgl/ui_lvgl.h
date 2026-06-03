#ifndef UI_LVGL_H
#define UI_LVGL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_IDLE,        // clock face, no active call
    SCREEN_DIALER,      // outgoing call being placed
    SCREEN_CALLING,     // outgoing, ringing
    SCREEN_INCOMING,    // incoming call (show Answer/Decline)
    SCREEN_ACTIVE       // call connected
} ui_screen_t;

// Action callbacks invoked when the user taps the on-screen call buttons.
// Wired by the app layer to sip_client_answer_call / sip_client_terminate_call
// (the UI component must not depend on the SIP component directly).
typedef void (*ui_action_cb_t)(void);

void ui_lvgl_init(void);
void ui_lvgl_set_action_cb(ui_action_cb_t answer_cb, ui_action_cb_t hangup_cb);
void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id);
void ui_lvgl_set_registered(bool registered);
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
