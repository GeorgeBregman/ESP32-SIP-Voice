#include "ui_lvgl.h"
#include "esp_log.h"
#include "config_manager.h"

// Check if LVGL is available via component manager
#if __has_include("lvgl.h")
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "display_tft.h"
#include "touch_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <time.h>
#include <stdio.h>

extern esp_lcd_panel_handle_t panel_handle; // from display_tft.c

static const char *TAG = "UI_LVGL";

// ---- Pick the largest fonts actually enabled in lv_conf.h ----
#if LV_FONT_MONTSERRAT_48
#  define FONT_XL &lv_font_montserrat_48
#elif LV_FONT_MONTSERRAT_40
#  define FONT_XL &lv_font_montserrat_40
#elif LV_FONT_MONTSERRAT_36
#  define FONT_XL &lv_font_montserrat_36
#elif LV_FONT_MONTSERRAT_28
#  define FONT_XL &lv_font_montserrat_28
#else
#  define FONT_XL LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_28
#  define FONT_L &lv_font_montserrat_28
#elif LV_FONT_MONTSERRAT_24
#  define FONT_L &lv_font_montserrat_24
#elif LV_FONT_MONTSERRAT_20
#  define FONT_L &lv_font_montserrat_20
#else
#  define FONT_L LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_18
#  define FONT_M &lv_font_montserrat_18
#elif LV_FONT_MONTSERRAT_16
#  define FONT_M &lv_font_montserrat_16
#else
#  define FONT_M LV_FONT_DEFAULT
#endif

// ---- display / driver state ----
static lv_disp_draw_buf_t disp_buf;
static lv_color_t *buf1;
static lv_disp_drv_t disp_drv;
static SemaphoreHandle_t lvgl_mux = NULL;
static int disp_w = 240, disp_h = 240;
static bool is_round = true;

static uint8_t current_theme = 0;   // 0=Voice Assistant, 1=Mobile OS, 2=Smart Speaker
static bool   s_registered = false;
static ui_screen_t s_screen = SCREEN_IDLE;

static ui_action_cb_t s_answer_cb = NULL;
static ui_action_cb_t s_hangup_cb = NULL;

// ---- shared widgets ----
static lv_obj_t *clock_label;   // HH:MM
static lv_obj_t *date_label;    // weekday / date
static lv_obj_t *sub_label;     // status line (registration / context)
static lv_obj_t *center_widget; // orb (VA) / avatar (Mobile) / ring (Echo)
static lv_obj_t *avatar_head;   // Mobile avatar inner
static lv_obj_t *caller_label;  // caller id / target
static lv_obj_t *state_label;   // "Speak Now" / "Incoming" / "Active"
static lv_obj_t *btn_answer;
static lv_obj_t *btn_end;

// ===== LVGL plumbing =====
bool ui_lvgl_lock(int timeout_ms) {
    if (!lvgl_mux) return true;
    TickType_t to = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, to) == pdTRUE;
}
void ui_lvgl_unlock(void) {
    if (lvgl_mux) xSemaphoreGiveRecursive(lvgl_mux);
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_tick_cb(void *arg) { (void)arg; lv_tick_inc(2); }

// Touch input device: bridges the XPT2046 driver to LVGL.
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    int16_t x = 0, y = 0;
    if (touch_driver_read(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_task(void *arg) {
    (void)arg;
    while (1) {
        if (ui_lvgl_lock(-1)) { lv_timer_handler(); ui_lvgl_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== helpers =====
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static void btn_answer_event(lv_event_t *e) { (void)e; if (s_answer_cb) s_answer_cb(); }
static void btn_end_event(lv_event_t *e)    { (void)e; if (s_hangup_cb) s_hangup_cb(); }

// Build the call-control buttons (shared across themes).
static void build_buttons(void) {
    // Answer (green, phone icon)
    btn_answer = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_answer, 64, 64);
    lv_obj_set_style_radius(btn_answer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_answer, lv_color_hex(0x34c759), 0);
    lv_obj_set_style_shadow_color(btn_answer, lv_color_hex(0x34c759), 0);
    lv_obj_set_style_shadow_width(btn_answer, 18, 0);
    lv_obj_add_event_cb(btn_answer, btn_answer_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = lv_label_create(btn_answer);
    lv_label_set_text(al, LV_SYMBOL_CALL);
    lv_obj_set_style_text_color(al, lv_color_white(), 0);
    lv_obj_center(al);

    // End / Decline (red)
    btn_end = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_end, 64, 64);
    lv_obj_set_style_radius(btn_end, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_end, lv_color_hex(0xff3b30), 0);
    lv_obj_set_style_shadow_color(btn_end, lv_color_hex(0xff3b30), 0);
    lv_obj_set_style_shadow_width(btn_end, 18, 0);
    lv_obj_add_event_cb(btn_end, btn_end_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(btn_end);
    lv_label_set_text(el, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(el, lv_color_white(), 0);
    lv_obj_center(el);
}

// ===== theme construction =====
static void build_theme(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    int cx = disp_w / 2;

    if (current_theme == 2) {
        // ---- Smart Speaker: neon ring + big clock ----
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        center_widget = lv_arc_create(scr);            // glowing outer ring
        lv_obj_set_size(center_widget, disp_w - 6, disp_h - 6);
        lv_obj_center(center_widget);
        lv_arc_set_rotation(center_widget, 270);
        lv_arc_set_bg_angles(center_widget, 0, 360);
        lv_arc_set_value(center_widget, 100);
        lv_obj_remove_style(center_widget, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(center_widget, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(center_widget, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_color(center_widget, lv_color_hex(0x00aaff), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(center_widget, 8, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(center_widget, lv_color_hex(0x00aaff), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(center_widget, 25, LV_PART_INDICATOR);

        clock_label = make_label(scr, FONT_XL, lv_color_white());
        lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, -10);
        date_label = make_label(scr, FONT_M, lv_color_hex(0xbbbbbb));
        lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 34);
        sub_label = make_label(scr, FONT_M, lv_color_hex(0x888888));
        lv_obj_align(sub_label, LV_ALIGN_CENTER, 0, 58);
        // Call info (shown instead of the clock while in a call).
        caller_label = make_label(scr, FONT_L, lv_color_white());
        lv_obj_align(caller_label, LV_ALIGN_CENTER, 0, -14);
        state_label = make_label(scr, FONT_M, lv_color_hex(0xcccccc));
        lv_obj_align(state_label, LV_ALIGN_CENTER, 0, 18);
    } else if (current_theme == 1) {
        // ---- Mobile OS: dark-blue gradient + avatar + caller ----
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1220), 0);
        lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x1a2a6c), 0);
        lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

        center_widget = lv_obj_create(scr);            // avatar disc
        lv_obj_set_size(center_widget, 76, 76);
        lv_obj_align(center_widget, LV_ALIGN_TOP_MID, 0, is_round ? 40 : 30);
        lv_obj_set_style_radius(center_widget, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(center_widget, lv_color_hex(0x9098a8), 0);
        lv_obj_set_style_border_width(center_widget, 0, 0);
        avatar_head = lv_obj_create(center_widget);    // head dot
        lv_obj_set_size(avatar_head, 26, 26);
        lv_obj_align(avatar_head, LV_ALIGN_CENTER, 0, -6);
        lv_obj_set_style_radius(avatar_head, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(avatar_head, lv_color_white(), 0);
        lv_obj_set_style_border_width(avatar_head, 0, 0);

        caller_label = make_label(scr, FONT_L, lv_color_white());
        lv_obj_align(caller_label, LV_ALIGN_CENTER, 0, -6);
        state_label = make_label(scr, FONT_M, lv_color_hex(0xc8c8c8));
        lv_obj_align(state_label, LV_ALIGN_CENTER, 0, 24);
        sub_label = make_label(scr, FONT_M, lv_color_hex(0x8893a8));
        lv_obj_align(sub_label, LV_ALIGN_CENTER, 0, 46);
        // small clock top
        clock_label = make_label(scr, FONT_M, lv_color_hex(0xaaaaaa));
        lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, is_round ? 14 : 6);
        date_label = NULL;
    } else {
        // ---- Voice Assistant: clock top, orb center, status bottom ----
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x101014), 0);
        lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

        clock_label = make_label(scr, FONT_L, lv_color_white());
        lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, is_round ? 34 : 18);
        date_label = make_label(scr, FONT_M, lv_color_hex(0xbbbbbb));
        lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, is_round ? 70 : 50);

        center_widget = lv_obj_create(scr);            // Siri-like orb
        lv_obj_set_size(center_widget, 84, 84);
        lv_obj_center(center_widget);
        lv_obj_set_style_radius(center_widget, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(center_widget, lv_color_hex(0x6e3cff), 0);
        lv_obj_set_style_bg_grad_color(center_widget, lv_color_hex(0x00d2ff), 0);
        lv_obj_set_style_bg_grad_dir(center_widget, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(center_widget, 0, 0);
        lv_obj_set_style_shadow_color(center_widget, lv_color_hex(0x4287f5), 0);
        lv_obj_set_style_shadow_width(center_widget, 40, 0);
        lv_obj_set_style_shadow_spread(center_widget, 4, 0);

        state_label = make_label(scr, FONT_M, lv_color_white());
        lv_obj_align(state_label, LV_ALIGN_CENTER, 0, 60);
        caller_label = make_label(scr, FONT_M, lv_color_hex(0xcccccc));
        lv_obj_align(caller_label, LV_ALIGN_CENTER, 0, 84);
        sub_label = NULL;
    }

    build_buttons();
}

// Position the call buttons for a given layout (1 or 2 visible).
static void layout_buttons(bool two) {
    int y = is_round ? -28 : -16;
    if (two) {
        lv_obj_align(btn_answer, LV_ALIGN_BOTTOM_MID, -45, y);
        lv_obj_align(btn_end,    LV_ALIGN_BOTTOM_MID,  45, y);
        lv_obj_clear_flag(btn_answer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(btn_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(btn_end, LV_ALIGN_BOTTOM_MID, 0, y);
    }
    lv_obj_clear_flag(btn_end, LV_OBJ_FLAG_HIDDEN);
}

static void hide_buttons(void) {
    if (btn_answer) lv_obj_add_flag(btn_answer, LV_OBJ_FLAG_HIDDEN);
    if (btn_end)    lv_obj_add_flag(btn_end, LV_OBJ_FLAG_HIDDEN);
}

// ===== clock =====
static void clock_update(void) {
    if (!clock_label) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    bool synced = (tm.tm_year + 1900) >= 2024;

    char buf[16];
    if (!synced) {
        lv_label_set_text(clock_label, "--:--");
    } else if (current_theme == 0) {
        int h12 = tm.tm_hour % 12; if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d %s", h12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
        lv_label_set_text(clock_label, buf);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        lv_label_set_text(clock_label, buf);
    }

    if (date_label && synced) {
        static const char *wd[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
        static const char *mo[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
        char d[24];
        snprintf(d, sizeof(d), "%s, %s %d", wd[tm.tm_wday % 7], mo[tm.tm_mon % 12], tm.tm_mday);
        lv_label_set_text(date_label, d);
    }
}

static void clock_timer_cb(lv_timer_t *t) { (void)t; clock_update(); }

// ===== screen state application (assumes lock held) =====
static void apply_screen(ui_screen_t screen, const char *caller_id) {
    s_screen = screen;
    bool in_call = (screen != SCREEN_IDLE);

    // Clock visible on idle (and as a small element on Mobile during call).
    if (clock_label) {
        if (current_theme == 1) lv_obj_clear_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
        else if (in_call) lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (date_label) {
        if (in_call) lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(date_label, LV_OBJ_FLAG_HIDDEN);
    }

    const char *state_txt = "";
    switch (screen) {
        case SCREEN_IDLE:     state_txt = current_theme == 0 ? "Tap to talk" : ""; break;
        case SCREEN_DIALER:   state_txt = "Dialing..."; break;
        case SCREEN_CALLING:  state_txt = "Calling..."; break;
        case SCREEN_INCOMING: state_txt = current_theme == 1 ? "Incoming Intercom Call" : "Incoming"; break;
        case SCREEN_ACTIVE:   state_txt = current_theme == 0 ? "Speak Now" : "Active"; break;
    }
    if (state_label) lv_label_set_text(state_label, state_txt);
    if (caller_label) lv_label_set_text(caller_label, (in_call && caller_id) ? caller_id : "");

    // Buttons
    if (btn_answer && btn_end) {
        if (screen == SCREEN_INCOMING)      layout_buttons(true);
        else if (screen == SCREEN_CALLING || screen == SCREEN_DIALER || screen == SCREEN_ACTIVE)
                                            layout_buttons(false);
        else                                hide_buttons();
    }

    // Central widget accent colour per state
    lv_color_t accent = (screen == SCREEN_INCOMING) ? lv_color_hex(0xff3b30)
                      : (screen == SCREEN_ACTIVE)   ? lv_color_hex(0x34c759)
                                                    : lv_color_hex(0x00aaff);
    if (current_theme == 2 && center_widget) {
        lv_obj_set_style_arc_color(center_widget, accent, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(center_widget, accent, LV_PART_INDICATOR);
    } else if (current_theme == 0 && center_widget) {
        lv_obj_set_style_shadow_color(center_widget, accent, 0);
    }

    // Idle status line
    if (sub_label && !in_call) {
        lv_label_set_text(sub_label, s_registered ? LV_SYMBOL_OK " Online" : LV_SYMBOL_WARNING " Offline");
        lv_obj_clear_flag(sub_label, LV_OBJ_FLAG_HIDDEN);
    } else if (sub_label && current_theme == 1) {
        lv_label_set_text(sub_label, "Front Door");
    } else if (sub_label) {
        lv_obj_add_flag(sub_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// ===== public API =====
void ui_lvgl_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL Interface...");

    hardware_settings_t hw;
    if (config_manager_load_hw(&hw) == ESP_OK) current_theme = hw.ui_theme;
    if (current_theme > 2) current_theme = 0;

    display_tft_get_resolution(&disp_w, &disp_h);
    is_round = display_tft_is_round();

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    lv_init();

    const int buf_lines = 40;
    size_t buf_px = (size_t)disp_w * buf_lines;
    buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1) { ESP_LOGE(TAG, "draw buffer alloc failed"); return; }
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, buf_px);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = disp_w;
    disp_drv.ver_res = disp_h;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch input device (XPT2046). Buttons need this; calibrate the raw bounds
    // in app_config.h (TOUCH_X_MIN/MAX, TOUCH_Y_MIN/MAX, TOUCH_SWAP_XY...).
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t tick_args = { .callback = &lvgl_tick_cb, .name = "lvgl_tick" };
    esp_timer_handle_t tick_timer = NULL;
    if (esp_timer_create(&tick_args, &tick_timer) == ESP_OK) {
        esp_timer_start_periodic(tick_timer, 2 * 1000);
    }

    build_theme();
    apply_screen(SCREEN_IDLE, NULL);
    clock_update();
    lv_timer_create(clock_timer_cb, 1000, NULL);

    xTaskCreate(lvgl_task, "lvgl", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "LVGL ready. Theme %d, %dx%d, round=%d", current_theme, disp_w, disp_h, is_round);
}

void ui_lvgl_set_action_cb(ui_action_cb_t answer_cb, ui_action_cb_t hangup_cb) {
    s_answer_cb = answer_cb;
    s_hangup_cb = hangup_cb;
}

void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) {
    if (!ui_lvgl_lock(200)) return;
    apply_screen(screen, caller_id);
    ui_lvgl_unlock();
}

void ui_lvgl_set_registered(bool registered) {
    s_registered = registered;
    if (!ui_lvgl_lock(200)) return;
    if (s_screen == SCREEN_IDLE) apply_screen(SCREEN_IDLE, NULL);
    ui_lvgl_unlock();
}

void ui_lvgl_update_energy(uint8_t energy_level) {
    if (!ui_lvgl_lock(20)) return;
    if (current_theme == 0 && center_widget) {
        lv_obj_set_style_shadow_width(center_widget, 30 + (energy_level / 4), 0);
        lv_obj_set_style_shadow_spread(center_widget, 2 + (energy_level / 16), 0);
    } else if (current_theme == 2 && center_widget) {
        lv_obj_set_style_arc_width(center_widget, 8 + (energy_level / 24), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(center_widget, 20 + (energy_level / 6), LV_PART_INDICATOR);
    }
    ui_lvgl_unlock();
}

#else

// Placeholder if LVGL is not installed
static const char *TAG = "UI_LVGL";
void ui_lvgl_init(void) { ESP_LOGW(TAG, "LVGL not found! Skipping UI init."); }
void ui_lvgl_set_action_cb(ui_action_cb_t answer_cb, ui_action_cb_t hangup_cb) { (void)answer_cb; (void)hangup_cb; }
void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) { (void)screen; (void)caller_id; }
void ui_lvgl_set_registered(bool registered) { (void)registered; }
void ui_lvgl_update_energy(uint8_t energy_level) { (void)energy_level; }
bool ui_lvgl_lock(int timeout_ms) { (void)timeout_ms; return true; }
void ui_lvgl_unlock(void) { }

#endif
