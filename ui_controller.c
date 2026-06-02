#include "ui_controller.h"
#include "app_config.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI_CTRL";
static sip_client_handle_t g_sip_client = NULL;
static app_settings_t *g_settings = NULL;

#ifdef CTRL_METHOD_BUTTONS
#include "driver/gpio.h"
static void button_task(void *arg) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = 1;
    while(1) {
        int state = gpio_get_level(BUTTON_GPIO);
        if (state == 0 && last_state == 1) {
            ESP_LOGI(TAG, "Button Pressed!");
            if (g_sip_client) {
                sip_call_state_t st = sip_client_get_call_state(g_sip_client);
                if (st == SIP_CALL_STATE_IDLE) {
                    sip_client_initiate_call(g_sip_client, SIP_TARGET_URI);
                } else if (st == SIP_CALL_STATE_INCOMING) {
                    sip_client_answer_call(g_sip_client);
                } else {
                    sip_client_terminate_call(g_sip_client);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        last_state = state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

#include "esp_http_server.h"

static httpd_handle_t server = NULL;

// Helper to URL-decode
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static esp_err_t index_get_handler(httpd_req_t *req) {
    char resp_str[1024];
    if (wifi_is_ap_mode()) {
        snprintf(resp_str, sizeof(resp_str), 
            "<html><body><h1>ESP32 SIP Setup (Captive Portal)</h1>"
            "<form method='POST' action='/setup'>"
            "WiFi SSID: <input type='text' name='ssid' value='%s'><br>"
            "WiFi Pass: <input type='password' name='pass' value='%s'><br>"
            "SIP Server: <input type='text' name='sip_server' value='%s'><br>"
            "SIP User: <input type='text' name='sip_user' value='%s'><br>"
            "SIP Pass: <input type='password' name='sip_pass' value='%s'><br>"
            "<input type='submit' value='Save & Restart'>"
            "</form></body></html>", 
            g_settings->wifi_ssid, g_settings->wifi_password, 
            g_settings->sip_server, g_settings->sip_user, g_settings->sip_password);
    } else {
        int st = g_sip_client ? sip_client_get_call_state(g_sip_client) : 0;
        snprintf(resp_str, sizeof(resp_str), 
            "<html><body><h1>ESP32 SIP Phone</h1>"
            "<p>Call State: %d</p>"
            "<form method='POST' action='/call'><input type='submit' value='Call'/></form>"
            "<form method='POST' action='/answer'><input type='submit' value='Answer'/></form>"
            "<form method='POST' action='/hangup'><input type='submit' value='Hangup'/></form>"
            "</body></html>", st);
    }
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t setup_post_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char ssid[32] = {0}, pass[64] = {0}, sip_server[32] = {0}, sip_user[32] = {0}, sip_pass[64] = {0};
    
    char *p = strtok(buf, "&");
    while (p) {
        char *k = p;
        char *v = strchr(p, '=');
        if (v) {
            *v = '\0';
            v++;
            if (strcmp(k, "ssid") == 0) url_decode(ssid, v);
            else if (strcmp(k, "pass") == 0) url_decode(pass, v);
            else if (strcmp(k, "sip_server") == 0) url_decode(sip_server, v);
            else if (strcmp(k, "sip_user") == 0) url_decode(sip_user, v);
            else if (strcmp(k, "sip_pass") == 0) url_decode(sip_pass, v);
        }
        p = strtok(NULL, "&");
    }

    strcpy(g_settings->wifi_ssid, ssid);
    strcpy(g_settings->wifi_password, pass);
    strcpy(g_settings->sip_server, sip_server);
    strcpy(g_settings->sip_user, sip_user);
    strcpy(g_settings->sip_password, sip_pass);

    config_manager_save(g_settings);

    httpd_resp_send(req, "Saved. Rebooting...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t action_post_handler(httpd_req_t *req) {
    if (!g_sip_client) return ESP_FAIL;
    if (strcmp(req->uri, "/call") == 0) {
        sip_client_initiate_call(g_sip_client, SIP_TARGET_URI);
    } else if (strcmp(req->uri, "/answer") == 0) {
        sip_client_answer_call(g_sip_client);
    } else if (strcmp(req->uri, "/hangup") == 0) {
        sip_client_terminate_call(g_sip_client);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // In captive portal, bind to any. Catch-all for HTTP.
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/*", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_setup = { .uri = "/setup", .method = HTTP_POST, .handler = setup_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_setup);
        
        if (!wifi_is_ap_mode()) {
            httpd_uri_t uri_call = { .uri = "/call", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_call);
            httpd_uri_t uri_answer = { .uri = "/answer", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_answer);
            httpd_uri_t uri_hangup = { .uri = "/hangup", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_hangup);
        }
    }
}

esp_err_t ui_controller_init(sip_client_handle_t sip_client, app_settings_t *settings) {
    g_sip_client = sip_client;
    g_settings = settings;
    ESP_LOGI(TAG, "Initializing UI Controller");
#ifdef CTRL_METHOD_BUTTONS
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
#endif
    
    // Always start webserver for Captive Portal or Web Control
    start_webserver();

#ifdef CTRL_METHOD_AUTO
    ESP_LOGI(TAG, "Auto-answer enabled. Handled by SIP Client.");
#endif
    return ESP_OK;
}
