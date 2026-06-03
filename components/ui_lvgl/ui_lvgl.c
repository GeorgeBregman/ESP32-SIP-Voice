#include "ui_lvgl.h"
#include "esp_log.h"
#include "sip_client.h"

// If LVGL is installed via idf.py add-dependency:
// #include "lvgl.h"

static const char *TAG = "UI_LVGL";

void ui_lvgl_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL User Interface...");
    
    // In a real LVGL application, we would:
    // 1. Initialize lvgl: lv_init()
    // 2. Register display buffer and flush_cb (via esp_lcd)
    // 3. Register input device (touch_driver_read)
    // 4. Create the screens (lv_obj_create)
    // 5. Start a FreeRTOS task to call lv_timer_handler() periodically
    
    ESP_LOGI(TAG, "LVGL setup deferred until component is downloaded.");
}

void ui_lvgl_switch_screen(ui_screen_t screen, const char *caller_id) {
    switch(screen) {
        case SCREEN_DIALER:
            ESP_LOGI(TAG, "Switching to SCREEN_DIALER");
            // lv_scr_load(dialer_screen);
            break;
        case SCREEN_CALLING:
            ESP_LOGI(TAG, "Switching to SCREEN_CALLING. Target: %s", caller_id ? caller_id : "Unknown");
            // lv_label_set_text(calling_label, caller_id);
            // lv_scr_load(calling_screen);
            break;
        case SCREEN_INCOMING:
            ESP_LOGI(TAG, "Switching to SCREEN_INCOMING. Caller: %s", caller_id ? caller_id : "Unknown");
            // lv_label_set_text(incoming_label, caller_id);
            // lv_scr_load(incoming_screen);
            break;
    }
}

// Example Button Callback for Dialer
/*
static void dialer_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        // Get button text, append to text area
        // If "Call" button, trigger sip_client_initiate_call()
    }
}
*/
