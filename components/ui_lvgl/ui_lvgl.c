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

static lv_obj_t * siri_orb;
static lv_obj_t * status_label;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    }
    lv_disp_flush_ready(drv);
}

void ui_lvgl_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL Apple-style Interface...");
    
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

    // Apple watchOS style deep black background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); 

    // Siri-like glowing orb in the center
    siri_orb = lv_obj_create(lv_scr_act());
    lv_obj_set_size(siri_orb, 100, 100);
    lv_obj_center(siri_orb);
    lv_obj_set_style_radius(siri_orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(siri_orb, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(siri_orb, LV_OPA_0, 0); // Hide actual background
    lv_obj_set_style_border_width(siri_orb, 0, 0);
    
    // Add intense soft shadow to act as the glowing aura
    lv_obj_set_style_shadow_color(siri_orb, lv_color_hex(0x4287f5), 0); // Apple Siri blue
    lv_obj_set_style_shadow_width(siri_orb, 60, 0);
    lv_obj_set_style_shadow_spread(siri_orb, 10, 0);
    lv_obj_set_style_shadow_opa(siri_orb, LV_OPA_TRANSP, 0); // Hidden initially

    // Status Label - San Francisco / Minimalist Style
    status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(status_label, "Готов"); // Cyrillic
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(status_label, LV_OPA_70, 0); // Frosted/Subtle
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 60); // Offset below orb

    ESP_LOGI(TAG, "LVGL setup complete.");
}

void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) {
    if (!status_label || !siri_orb) return;
    
    switch(screen) {
        case SCREEN_DIALER:
            lv_label_set_text(status_label, "Набор...");
            lv_obj_set_style_shadow_color(siri_orb, lv_color_hex(0x34c759), 0); // Apple Green
            lv_obj_set_style_shadow_opa(siri_orb, LV_OPA_70, 0);
            break;
        case SCREEN_CALLING:
            lv_label_set_text_fmt(status_label, "Звонок\n%s", caller_id ? caller_id : "");
            lv_obj_set_style_shadow_color(siri_orb, lv_color_hex(0x007aff), 0); // Apple Blue
            lv_obj_set_style_shadow_opa(siri_orb, LV_OPA_100, 0);
            break;
        case SCREEN_INCOMING:
            lv_label_set_text_fmt(status_label, "Входящий\n%s", caller_id ? caller_id : "");
            lv_obj_set_style_shadow_color(siri_orb, lv_color_hex(0xff3b30), 0); // Apple Red
            lv_obj_set_style_shadow_opa(siri_orb, LV_OPA_100, 0);
            break;
    }
}

// Function to update the orb based on audio energy
void ui_lvgl_update_energy(uint8_t energy_level) {
    if(siri_orb) {
        // Map 0-255 energy to shadow spread/width for a breathing glow effect
        lv_coord_t width = 40 + (energy_level / 4);
        lv_coord_t spread = 5 + (energy_level / 8);
        lv_obj_set_style_shadow_width(siri_orb, width, 0);
        lv_obj_set_style_shadow_spread(siri_orb, spread, 0);
    }
}

#else

// Placeholder if LVGL is not installed
static const char *TAG = "UI_LVGL";
void ui_lvgl_init(void) { ESP_LOGW(TAG, "LVGL not found! Skipping UI init."); }
void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) { }
void ui_lvgl_update_energy(uint8_t energy_level) { }

#endif
