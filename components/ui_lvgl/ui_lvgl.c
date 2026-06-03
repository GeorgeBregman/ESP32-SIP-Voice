#include "ui_lvgl.h"
#include "esp_log.h"
#include "sip_client.h"
#include "config_manager.h"

// Check if LVGL is available via component manager
#if __has_include("lvgl.h")
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

extern esp_lcd_panel_handle_t panel_handle; // from display_tft.c

static const char *TAG = "UI_LVGL";

static lv_disp_draw_buf_t disp_buf;
static lv_color_t *buf1;
static lv_disp_drv_t disp_drv;

static uint8_t current_theme = 0; // 0=Siri, 1=iPhone, 2=Echo

// Siri objects
static lv_obj_t * siri_orb;

// iPhone Call Screen objects
static lv_obj_t * iphone_avatar;
static lv_obj_t * iphone_bg;

// Echo objects
static lv_obj_t * echo_ring; // For circular screens (Spot)
static lv_obj_t * echo_bar;  // For rectangular screens (Show)

static lv_obj_t * status_label;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    }
    lv_disp_flush_ready(drv);
}

static void init_theme_siri() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); 
    siri_orb = lv_obj_create(lv_scr_act());
    lv_obj_set_size(siri_orb, 100, 100);
    lv_obj_center(siri_orb);
    lv_obj_set_style_radius(siri_orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(siri_orb, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(siri_orb, LV_OPA_0, 0);
    lv_obj_set_style_border_width(siri_orb, 0, 0);
    lv_obj_set_style_shadow_color(siri_orb, lv_color_hex(0x4287f5), 0);
    lv_obj_set_style_shadow_width(siri_orb, 60, 0);
    lv_obj_set_style_shadow_spread(siri_orb, 10, 0);
    lv_obj_set_style_shadow_opa(siri_orb, LV_OPA_TRANSP, 0);
}

static void init_theme_iphone() {
    // Blurred/Gradient Background
    iphone_bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(iphone_bg, 240, 240);
    lv_obj_center(iphone_bg);
    lv_obj_set_style_bg_grad_color(iphone_bg, lv_color_hex(0x1a2a6c), 0);
    lv_obj_set_style_bg_color(iphone_bg, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_grad_dir(iphone_bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(iphone_bg, 0, 0);

    // Profile Avatar
    iphone_avatar = lv_obj_create(iphone_bg);
    lv_obj_set_size(iphone_avatar, 80, 80);
    lv_obj_align(iphone_avatar, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_radius(iphone_avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(iphone_avatar, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(iphone_avatar, 0, 0);
}

static void init_theme_echo() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    
    // Auto-detect Geometry
    lv_disp_t * disp = lv_disp_get_default();
    lv_coord_t w = lv_disp_get_hor_res(disp);
    lv_coord_t h = lv_disp_get_ver_res(disp);
    
    if (w == h) {
        // Echo Spot (Circular)
        echo_ring = lv_arc_create(lv_scr_act());
        lv_obj_set_size(echo_ring, w, h);
        lv_obj_center(echo_ring);
        lv_arc_set_rotation(echo_ring, 270);
        lv_arc_set_bg_angles(echo_ring, 0, 360);
        lv_obj_remove_style(echo_ring, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(echo_ring, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_set_style_arc_color(echo_ring, lv_color_hex(0x00ffff), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(echo_ring, 12, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(echo_ring, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_arc_width(echo_ring, 0, LV_PART_MAIN);
    } else {
        // Echo Show (Rectangular Dashboard)
        // Background gradient
        lv_obj_t * bg = lv_obj_create(lv_scr_act());
        lv_obj_set_size(bg, w, h);
        lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x1a2a6c), 0);
        lv_obj_set_style_bg_color(bg, lv_color_hex(0x112233), 0);
        lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(bg, 0, 0);
        
        // Widget Card placeholder
        lv_obj_t * widget = lv_obj_create(bg);
        lv_obj_set_size(widget, w / 2 - 20, h - 40);
        lv_obj_align(widget, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_radius(widget, 10, 0);
        lv_obj_set_style_bg_color(widget, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(widget, LV_OPA_20, 0);
        lv_obj_set_style_border_width(widget, 0, 0);
        
        // Light bar at the bottom
        echo_bar = lv_obj_create(lv_scr_act());
        lv_obj_set_size(echo_bar, w, 6);
        lv_obj_align(echo_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(echo_bar, lv_color_hex(0x00ffff), 0);
        lv_obj_set_style_border_width(echo_bar, 0, 0);
        lv_obj_set_style_shadow_color(echo_bar, lv_color_hex(0x00ffff), 0);
        lv_obj_set_style_shadow_width(echo_bar, 20, 0);
        lv_obj_set_style_shadow_spread(echo_bar, 5, 0);
    }
}

void ui_lvgl_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL Interface...");
    
    hardware_settings_t hw;
    if (config_manager_load_hw(&hw) == ESP_OK) {
        current_theme = hw.ui_theme;
    }
    
    lv_init();

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

    if (current_theme == 0) init_theme_siri();
    else if (current_theme == 1) init_theme_iphone();
    else if (current_theme == 2) init_theme_echo();

    status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(status_label, "Готов");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffffff), 0);
    
    if (current_theme == 0) {
        lv_obj_set_style_text_opa(status_label, LV_OPA_70, 0);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 60);
    } else if (current_theme == 1) {
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 20); // below avatar
    } else {
        lv_obj_center(status_label);
    }

    ESP_LOGI(TAG, "LVGL setup complete. Theme: %d", current_theme);
}

void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) {
    if (!status_label) return;
    
    const char* txt = "Готов";
    lv_color_t theme_color = lv_color_hex(0xffffff);

    switch(screen) {
        case SCREEN_DIALER:
            txt = "Набор...";
            theme_color = lv_color_hex(0x34c759);
            break;
        case SCREEN_CALLING:
            txt = caller_id ? caller_id : "Звонок";
            theme_color = lv_color_hex(0x007aff);
            break;
        case SCREEN_INCOMING:
            txt = caller_id ? caller_id : "Входящий";
            theme_color = lv_color_hex(0xff3b30);
            break;
    }

    lv_label_set_text(status_label, txt);

    if (current_theme == 0 && siri_orb) {
        lv_obj_set_style_shadow_color(siri_orb, theme_color, 0);
        lv_obj_set_style_shadow_opa(siri_orb, screen == SCREEN_DIALER ? LV_OPA_0 : LV_OPA_100, 0);
    } else if (current_theme == 1 && iphone_bg) {
        // Change iPhone gradient based on status
        lv_obj_set_style_bg_grad_color(iphone_bg, theme_color, 0);
    } else if (current_theme == 2) {
        if (echo_ring) {
            lv_obj_set_style_arc_color(echo_ring, theme_color, LV_PART_INDICATOR);
        } else if (echo_bar) {
            lv_obj_set_style_bg_color(echo_bar, theme_color, 0);
            lv_obj_set_style_shadow_color(echo_bar, theme_color, 0);
        }
    }
}

void ui_lvgl_update_energy(uint8_t energy_level) {
    if (current_theme == 0 && siri_orb) {
        lv_coord_t width = 40 + (energy_level / 4);
        lv_coord_t spread = 5 + (energy_level / 8);
        lv_obj_set_style_shadow_width(siri_orb, width, 0);
        lv_obj_set_style_shadow_spread(siri_orb, spread, 0);
    } else if (current_theme == 2) {
        if (echo_ring) {
            lv_obj_set_style_arc_width(echo_ring, 12 + (energy_level / 16), LV_PART_INDICATOR);
            lv_arc_set_value(echo_ring, (energy_level * 100) / 255);
        } else if (echo_bar) {
            // Echo bar breathes vertically and glows
            lv_obj_set_style_shadow_width(echo_bar, 10 + (energy_level / 4), 0);
            lv_obj_set_style_shadow_spread(echo_bar, 2 + (energy_level / 10), 0);
        }
    }
}

#else

// Placeholder if LVGL is not installed
static const char *TAG = "UI_LVGL";
void ui_lvgl_init(void) { ESP_LOGW(TAG, "LVGL not found! Skipping UI init."); }
void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) { }
void ui_lvgl_update_energy(uint8_t energy_level) { }

#endif
