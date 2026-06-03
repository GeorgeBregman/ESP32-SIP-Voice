#include "ui_controller.h"
#include "app_config.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "phonebook.h"
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
    char *resp_str = calloc(1, 4096);
    if (!resp_str) return ESP_FAIL;

    if (wifi_is_ap_mode()) {
        snprintf(resp_str, 4096, 
            "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>ESP32 SIP Setup</title><style>@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap');body{margin:0;padding:0;font-family:'Inter',sans-serif;background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);height:100vh;display:flex;justify-content:center;align-items:center;color:#fff;}.card{background:rgba(255,255,255,0.1);backdrop-filter:blur(10px);border-radius:16px;padding:40px;box-shadow:0 8px 32px 0 rgba(0,0,0,0.37);border:1px solid rgba(255,255,255,0.18);width:100%%;max-width:320px;}h1{text-align:center;margin-top:0;font-size:24px;font-weight:600;letter-spacing:1px;}.form-group{margin-bottom:16px;}label{display:block;font-size:13px;margin-bottom:6px;color:#ccc;}input[type='text'],input[type='password']{width:100%%;padding:10px;border:none;border-radius:8px;background:rgba(255,255,255,0.05);color:#fff;font-size:15px;box-sizing:border-box;transition:all 0.3s;border:1px solid rgba(255,255,255,0.1);}input:focus{outline:none;background:rgba(255,255,255,0.15);border-color:#00d2ff;box-shadow:0 0 10px rgba(0,210,255,0.5);}.btn{width:100%%;padding:14px;background:linear-gradient(90deg,#00d2ff 0%%,#3a7bd5 100%%);border:none;border-radius:8px;color:white;font-size:16px;font-weight:600;cursor:pointer;margin-top:10px;}</style></head><body><div class='card'><h1>ESP32 SIP Setup</h1><form method='POST' action='/setup'><div class='form-group'><label>WiFi SSID</label><input type='text' name='ssid' value='%s'></div><div class='form-group'><label>WiFi Password</label><input type='password' name='pass' value='%s'></div><div class='form-group'><label>SIP Server</label><input type='text' name='sip_server' value='%s'></div><div class='form-group'><label>SIP Username</label><input type='text' name='sip_user' value='%s'></div><div class='form-group'><label>SIP Password</label><input type='password' name='sip_pass' value='%s'></div><button type='submit' class='btn'>Save & Restart</button></form><br><center><a href='/hardware' style='color:#00d2ff;'>Advanced Hardware Config</a></center></div></body></html>",
            g_settings->wifi_ssid, g_settings->wifi_password, 
            g_settings->sip_server, g_settings->sip_user, g_settings->sip_password);
    } else {
        int st = g_sip_client ? sip_client_get_call_state(g_sip_client) : 0;
        snprintf(resp_str, 4096, 
            "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>ESP32 SIP Phone</title><style>@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap');body{margin:0;font-family:'Inter',sans-serif;background:linear-gradient(135deg,#141e30,#243b55);height:100vh;display:flex;justify-content:center;align-items:center;color:#fff;}.card{background:rgba(255,255,255,0.05);backdrop-filter:blur(15px);border-radius:20px;padding:40px;text-align:center;box-shadow:0 10px 30px rgba(0,0,0,0.5);border:1px solid rgba(255,255,255,0.1);width:100%%;max-width:320px;}h1{margin-top:0;font-size:26px;}.status{font-size:18px;margin-bottom:30px;color:#00d2ff;font-weight:600;}.btn{width:100%%;padding:15px;margin-bottom:15px;border:none;border-radius:10px;color:white;font-size:16px;font-weight:600;cursor:pointer;}.btn-call{background:linear-gradient(90deg,#11998e,#38ef7d);}.btn-answer{background:linear-gradient(90deg,#2193b0,#6dd5ed);}.btn-hangup{background:linear-gradient(90deg,#cb2d3e,#ef473a);}</style></head><body><div class='card'><h1>ESP32 Intercom</h1><div class='status'>State: %d</div><form method='POST' action='/call'><button type='submit' class='btn btn-call'>Call</button></form><form method='POST' action='/answer'><button type='submit' class='btn btn-answer'>Answer</button></form><form method='POST' action='/hangup'><button type='submit' class='btn btn-hangup'>Hangup</button></form><br><a href='/phonebook' style='color:#fff;'>Phonebook</a> | <a href='/hardware' style='color:#ccc;'>HW Config</a></div></body></html>", st);
    }
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
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

static esp_err_t phonebook_get_handler(httpd_req_t *req) {
    char *resp_str = calloc(1, 4096);
    if (!resp_str) return ESP_FAIL;
    
    int len = snprintf(resp_str, 4096, "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Phonebook</title></head><body><h1>Phonebook</h1><ul>");
    phonebook_entry_t entry;
    for(int i=0; i<MAX_PHONEBOOK_ENTRIES; i++) {
        if(phonebook_load_entry(i, &entry)) {
            len += snprintf(resp_str + len, 4096 - len, "<li>[%d] %s : %s <form method='POST' action='/pb_del'><input type='hidden' name='id' value='%d'><button type='submit'>Del</button></form></li>", i, entry.name, entry.uri, i);
        }
    }
    len += snprintf(resp_str + len, 4096 - len, "</ul><h2>Add Entry</h2><form method='POST' action='/pb_add'><input type='text' name='name' placeholder='Name'><input type='text' name='uri' placeholder='sip:1000@1.2.3.4'><input type='number' name='id' placeholder='Speed Dial 0-9' min='0' max='9'><button type='submit'>Save</button></form><br><a href='/'>Back</a></body></html>");
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    return ESP_OK;
}

static esp_err_t pb_add_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        char name[32]={0}, uri[64]={0}, id_str[4]={0};
        char *p = strtok(buf, "&");
        while(p) {
            char *v = strchr(p, '=');
            if(v) { *v++ = '\0';
                if(strcmp(p, "name")==0) url_decode(name, v);
                else if(strcmp(p, "uri")==0) url_decode(uri, v);
                else if(strcmp(p, "id")==0) url_decode(id_str, v);
            }
            p = strtok(NULL, "&");
        }
        int id = atoi(id_str);
        phonebook_save_entry(id, name, uri);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/phonebook");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t pb_del_post_handler(httpd_req_t *req) {
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        char *v = strchr(buf, '=');
        if(v) phonebook_clear_entry(atoi(v+1));
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/phonebook");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t hardware_get_handler(httpd_req_t *req) {
    char *resp_str = calloc(1, 4096);
    if (!resp_str) return ESP_FAIL;

    hardware_settings_t hw;
    config_manager_load_hw(&hw);

    snprintf(resp_str, 4096, "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>ESP32 SIP Hardware Config</title><style>body{font-family:sans-serif;background:#222;color:#eee;} .c{max-width:400px;margin:auto;background:#333;padding:20px;border-radius:10px;} input,select{width:100%%;margin-bottom:10px;} button{width:100%%;padding:10px;}</style></head><body><div class='c'><h2>HW Config</h2><form method='POST' action='/hardware'><label>I2S BCK:</label><input type='number' name='bck' value='%d'><br><label>I2S WS:</label><input type='number' name='ws' value='%d'><br><label>I2S DOUT:</label><input type='number' name='dout' value='%d'><br><label>I2S DIN:</label><input type='number' name='din' value='%d'><br><label>I2S MCLK:</label><input type='number' name='mclk' value='%d'><br><label>I2C SDA:</label><input type='number' name='sda' value='%d'><br><label>I2C SCL:</label><input type='number' name='scl' value='%d'><br><h3>UI Theme</h3><select name='ui_theme'><option value='0' %s>Apple Siri</option><option value='1' %s>iPhone Call</option><option value='2' %s>Amazon Echo</option></select><br><br><button type='submit'>Save</button></form></div></body></html>",
        hw.pin_i2s_bck, hw.pin_i2s_ws, hw.pin_i2s_dout, hw.pin_i2s_din, hw.pin_i2s_mclk, hw.pin_i2c_sda, hw.pin_i2c_scl,
        hw.ui_theme == 0 ? "selected" : "", hw.ui_theme == 1 ? "selected" : "", hw.ui_theme == 2 ? "selected" : "");

    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    return ESP_OK;
}

static esp_err_t hardware_post_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    hardware_settings_t hw;
    config_manager_load_hw(&hw); // Load current first

    char *p = strtok(buf, "&");
    while (p) {
        char *v = strchr(p, '=');
        if (v) {
            *v++ = '\0';
            if (strcmp(p, "bck") == 0) hw.pin_i2s_bck = atoi(v);
            else if (strcmp(p, "ws") == 0) hw.pin_i2s_ws = atoi(v);
            else if (strcmp(p, "dout") == 0) hw.pin_i2s_dout = atoi(v);
            else if (strcmp(p, "din") == 0) hw.pin_i2s_din = atoi(v);
            else if (strcmp(p, "mclk") == 0) hw.pin_i2s_mclk = atoi(v);
            else if (strcmp(p, "sda") == 0) hw.pin_i2c_sda = atoi(v);
            else if (strcmp(p, "scl") == 0) hw.pin_i2c_scl = atoi(v);
            else if (strcmp(p, "ui_theme") == 0) hw.ui_theme = atoi(v);
        }
        p = strtok(NULL, "&");
    }

    config_manager_save_hw(&hw);

    httpd_resp_send(req, "HW Saved. Rebooting...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // In captive portal, bind to any. Catch-all for HTTP.
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_setup = { .uri = "/setup", .method = HTTP_POST, .handler = setup_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_setup);
        httpd_uri_t uri_hw_get = { .uri = "/hardware", .method = HTTP_GET, .handler = hardware_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_hw_get);
        httpd_uri_t uri_hw_post = { .uri = "/hardware", .method = HTTP_POST, .handler = hardware_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_hw_post);
        
        if (!wifi_is_ap_mode()) {
            httpd_uri_t uri_call = { .uri = "/call", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_call);
            httpd_uri_t uri_answer = { .uri = "/answer", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_answer);
            httpd_uri_t uri_hangup = { .uri = "/hangup", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_hangup);
            
            httpd_uri_t uri_pb = { .uri = "/phonebook", .method = HTTP_GET, .handler = phonebook_get_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_pb);
            httpd_uri_t uri_pb_add = { .uri = "/pb_add", .method = HTTP_POST, .handler = pb_add_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_pb_add);
            httpd_uri_t uri_pb_del = { .uri = "/pb_del", .method = HTTP_POST, .handler = pb_del_post_handler, .user_ctx = NULL };
            httpd_register_uri_handler(server, &uri_pb_del);
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

void ui_controller_wake_word_trigger(void) {
    if (g_sip_client) {
        sip_call_state_t st = sip_client_get_call_state(g_sip_client);
        if (st == SIP_CALL_STATE_IDLE) {
            ESP_LOGI(TAG, "Wake word triggered! Initiating call...");
            sip_client_initiate_call(g_sip_client, SIP_TARGET_URI);
        }
    }
}
