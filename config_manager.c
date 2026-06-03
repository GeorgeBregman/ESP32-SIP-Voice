#include "config_manager.h"
#include "app_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CONFIG";
static const char *NVS_NAMESPACE = "sip_phone";

esp_err_t config_manager_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t config_manager_load(app_settings_t *settings) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS not found, using defaults");
        // Apply defaults from app_config.h
        strncpy(settings->wifi_ssid, WIFI_SSID, sizeof(settings->wifi_ssid));
        strncpy(settings->wifi_password, WIFI_PASSWORD, sizeof(settings->wifi_password));
        strncpy(settings->sip_server, SIP_SERVER_IP, sizeof(settings->sip_server));
        strncpy(settings->sip_user, SIP_USER, sizeof(settings->sip_user));
        strncpy(settings->sip_password, SIP_PASSWORD, sizeof(settings->sip_password));
        return ESP_OK; // Return OK with defaults
    }

    size_t len = sizeof(settings->wifi_ssid);
    nvs_get_str(my_handle, "wifi_ssid", settings->wifi_ssid, &len);
    len = sizeof(settings->wifi_password);
    nvs_get_str(my_handle, "wifi_pass", settings->wifi_password, &len);
    len = sizeof(settings->sip_server);
    nvs_get_str(my_handle, "sip_server", settings->sip_server, &len);
    len = sizeof(settings->sip_user);
    nvs_get_str(my_handle, "sip_user", settings->sip_user, &len);
    len = sizeof(settings->sip_password);
    nvs_get_str(my_handle, "sip_pass", settings->sip_password, &len);

    nvs_close(my_handle);
    ESP_LOGI(TAG, "Settings loaded from NVS");
    return ESP_OK;
}

esp_err_t config_manager_save(const app_settings_t *settings) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    nvs_set_str(my_handle, "wifi_ssid", settings->wifi_ssid);
    nvs_set_str(my_handle, "wifi_pass", settings->wifi_password);
    nvs_set_str(my_handle, "sip_server", settings->sip_server);
    nvs_set_str(my_handle, "sip_user", settings->sip_user);
    nvs_set_str(my_handle, "sip_pass", settings->sip_password);

    err = nvs_commit(my_handle);
    nvs_close(my_handle);
    ESP_LOGI(TAG, "Settings saved to NVS");
    return err;
}

esp_err_t config_manager_load_hw(hardware_settings_t *hw_settings) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // Set defaults (fallback to app_config.h values if possible, or -1 if unassigned)
    hw_settings->pin_i2s_bck = -1;
    hw_settings->pin_i2s_ws = -1;
    hw_settings->pin_i2s_dout = -1;
    hw_settings->pin_i2s_din = -1;
    hw_settings->pin_i2s_mclk = -1;
    hw_settings->pin_i2c_sda = -1;
    hw_settings->pin_i2c_scl = -1;
    hw_settings->pin_spi_mosi = -1;
    hw_settings->pin_spi_miso = -1;
    hw_settings->pin_spi_clk = -1;
    hw_settings->pin_tft_cs = -1;
    hw_settings->pin_tft_dc = -1;
    hw_settings->pin_tft_rst = -1;
    hw_settings->pin_touch_cs = -1;
    hw_settings->pin_touch_irq = -1;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Hardware config not initialized yet in NVS. Using defaults.");
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t required_size = sizeof(hardware_settings_t);
    err = nvs_get_blob(my_handle, "hw_settings", hw_settings, &required_size);
    if (err != ESP_OK || required_size != sizeof(hardware_settings_t)) {
        ESP_LOGI(TAG, "No valid HW settings in NVS. Using defaults.");
    } else {
        ESP_LOGI(TAG, "Hardware settings loaded from NVS successfully.");
    }

    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t config_manager_save_hw(const hardware_settings_t *hw_settings) {
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(my_handle, "hw_settings", hw_settings, sizeof(hardware_settings_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save HW settings blob");
    }

    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit HW settings");
    }

    nvs_close(my_handle);
    ESP_LOGI(TAG, "Hardware settings saved to NVS successfully.");
    return ESP_OK;
}

void config_manager_reset(void) {
    nvs_handle_t my_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "NVS reset complete");
    }
}
