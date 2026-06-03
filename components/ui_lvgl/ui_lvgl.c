#include "ui_lvgl.h"
#include "esp_log.h"
#include "sip_client.h"

// Check if LVGL is available via component manager
#if __has_include("lvgl.h")
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

extern esp_lcd_panel_handle_t panel_handle; // from display_tft.c

static const char *TAG = "UI_LVGL";

static lv_disp_draw_buf_t disp_buf;
static lv_color_t *buf1;
static lv_disp_drv_t disp_drv;

static lv_obj_t * ai_face_arc;
static lv_obj_t * status_label;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    }
    lv_disp_flush_ready(drv);
}

void ui_lvgl_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL User Interface...");
    
    lv_init();

    // Allocate draw buffer (1/10th of screen size)
    buf1 = heap_caps_malloc(240 * 240 * sizeof(lv_color_t) / 10, MALLOC_CAP_DMA);
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer");
        return;
    }
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, 240 * 240 / 10);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Build the AI Face UI (Glassmorphism + Neon)
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0f2027), 0); // Dark theme
    lv_obj_set_style_bg_grad_color(lv_scr_act(), lv_color_hex(0x203a43), 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);

    // Arc visualizer
    ai_face_arc = lv_arc_create(lv_scr_act());
    lv_obj_set_size(ai_face_arc, 200, 200);
    lv_arc_set_rotation(ai_face_arc, 270);
    lv_arc_set_bg_angles(ai_face_arc, 0, 360);
    lv_obj_remove_style(ai_face_arc, NULL, LV_PART_KNOB);   // remove knob
    lv_obj_clear_flag(ai_face_arc, LV_OBJ_FLAG_CLICKABLE);  // not clickable
    lv_obj_center(ai_face_arc);
    
    // Style the arc
    lv_obj_set_style_arc_color(ai_face_arc, lv_color_hex(0x00d2ff), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ai_face_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ai_face_arc, lv_color_hex(0x1a2a6c), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ai_face_arc, 4, LV_PART_MAIN);

    // Status Label
    status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(status_label, "ГОТОВ"); // Cyrillic ready
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(status_label);

    ESP_LOGI(TAG, "LVGL setup complete.");
}

void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) {
    if (!status_label) return;
    
    switch(screen) {
        case SCREEN_DIALER:
            lv_label_set_text(status_label, "НАБОР...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00d2ff), 0);
            lv_arc_set_value(ai_face_arc, 50); // Just a visual change
            break;
        case SCREEN_CALLING:
            lv_label_set_text_fmt(status_label, "ЗВОНОК\n%s", caller_id ? caller_id : "");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x3a7bd5), 0);
            lv_arc_set_value(ai_face_arc, 80);
            break;
        case SCREEN_INCOMING:
            lv_label_set_text_fmt(status_label, "ВХОДЯЩИЙ\n%s", caller_id ? caller_id : "");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xff0055), 0);
            lv_arc_set_value(ai_face_arc, 100);
            break;
    }
}

// Function to update the visualizer based on audio energy
void ui_lvgl_update_energy(uint8_t energy_level) {
    if(ai_face_arc) {
        // Map 0-255 energy to 0-100 arc value
        lv_arc_set_value(ai_face_arc, (energy_level * 100) / 255);
    }
}

#else

// Placeholder if LVGL is not installed
static const char *TAG = "UI_LVGL";
void ui_lvgl_init(void) { ESP_LOGW(TAG, "LVGL not found! Skipping UI init."); }
void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) { }
void ui_lvgl_update_energy(uint8_t energy_level) { }

#endif
