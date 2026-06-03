#include "phonebook.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PHONEBOOK";
#define NVS_NAMESPACE "phonebook"

void phonebook_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "Phonebook NVS initialized");
}

bool phonebook_save_entry(uint8_t index, const char* name, const char* uri) {
    if (index >= MAX_PHONEBOOK_ENTRIES || name == NULL || uri == NULL) return false;

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    char key_name[16];
    char key_uri[16];
    snprintf(key_name, sizeof(key_name), "name_%d", index);
    snprintf(key_uri, sizeof(key_uri), "uri_%d", index);

    err = nvs_set_str(my_handle, key_name, name);
    if (err == ESP_OK) {
        err = nvs_set_str(my_handle, key_uri, uri);
    }
    
    if (err == ESP_OK) {
        nvs_commit(my_handle);
        ESP_LOGI(TAG, "Saved entry %d: %s -> %s", index, name, uri);
    } else {
        ESP_LOGE(TAG, "Failed to save entry %d", index);
    }
    
    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool phonebook_load_entry(uint8_t index, phonebook_entry_t* entry) {
    if (index >= MAX_PHONEBOOK_ENTRIES || entry == NULL) return false;

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;

    char key_name[16];
    char key_uri[16];
    snprintf(key_name, sizeof(key_name), "name_%d", index);
    snprintf(key_uri, sizeof(key_uri), "uri_%d", index);

    size_t required_size;
    err = nvs_get_str(my_handle, key_name, NULL, &required_size);
    if (err == ESP_OK && required_size <= MAX_NAME_LEN) {
        nvs_get_str(my_handle, key_name, entry->name, &required_size);
    } else {
        nvs_close(my_handle);
        return false;
    }

    err = nvs_get_str(my_handle, key_uri, NULL, &required_size);
    if (err == ESP_OK && required_size <= MAX_URI_LEN) {
        nvs_get_str(my_handle, key_uri, entry->uri, &required_size);
    } else {
        nvs_close(my_handle);
        return false;
    }

    nvs_close(my_handle);
    return true;
}

bool phonebook_clear_entry(uint8_t index) {
    if (index >= MAX_PHONEBOOK_ENTRIES) return false;

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return false;

    char key_name[16];
    char key_uri[16];
    snprintf(key_name, sizeof(key_name), "name_%d", index);
    snprintf(key_uri, sizeof(key_uri), "uri_%d", index);

    nvs_erase_key(my_handle, key_name);
    nvs_erase_key(my_handle, key_uri);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    
    ESP_LOGI(TAG, "Cleared entry %d", index);
    return true;
}

uint8_t phonebook_get_count(void) {
    uint8_t count = 0;
    phonebook_entry_t temp;
    for (uint8_t i = 0; i < MAX_PHONEBOOK_ENTRIES; i++) {
        if (phonebook_load_entry(i, &temp)) {
            count++;
        }
    }
    return count;
}
