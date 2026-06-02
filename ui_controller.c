#include "ui_controller.h"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI_CTRL";
static sip_client_handle_t g_sip_client = NULL;

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
            // Button pressed
            ESP_LOGI(TAG, "Button Pressed!");
            sip_call_state_t st = sip_client_get_call_state(g_sip_client);
            if (st == SIP_CALL_STATE_IDLE) {
                sip_client_initiate_call(g_sip_client, SIP_TARGET_URI);
            } else if (st == SIP_CALL_STATE_INCOMING) {
                sip_client_answer_call(g_sip_client);
            } else {
                sip_client_terminate_call(g_sip_client);
            }
            vTaskDelay(pdMS_TO_TICKS(500)); // Debounce
        }
        last_state = state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

#ifdef CTRL_METHOD_WEB
#include "esp_http_server.h"

static httpd_handle_t server = NULL;

static esp_err_t index_get_handler(httpd_req_t *req) {
    sip_call_state_t st = sip_client_get_call_state(g_sip_client);
    char resp_str[512];
    snprintf(resp_str, sizeof(resp_str), 
        "<html><body><h1>ESP32 SIP Phone</h1>"
        "<p>State: %d</p>"
        "<form method='POST' action='/call'><input type='submit' value='Call'/></form>"
        "<form method='POST' action='/answer'><input type='submit' value='Answer'/></form>"
        "<form method='POST' action='/hangup'><input type='submit' value='Hangup'/></form>"
        "</body></html>", st);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t action_post_handler(httpd_req_t *req) {
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
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_call = { .uri = "/call", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_call);
        httpd_uri_t uri_answer = { .uri = "/answer", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_answer);
        httpd_uri_t uri_hangup = { .uri = "/hangup", .method = HTTP_POST, .handler = action_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_hangup);
    }
}
#endif

esp_err_t ui_controller_init(sip_client_handle_t sip_client) {
    g_sip_client = sip_client;
    ESP_LOGI(TAG, "Initializing UI Controller");
#ifdef CTRL_METHOD_BUTTONS
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
#endif
#ifdef CTRL_METHOD_WEB
    start_webserver();
#endif
#ifdef CTRL_METHOD_AUTO
    ESP_LOGI(TAG, "Auto-answer enabled. Handled by SIP Client.");
#endif
    return ESP_OK;
}
