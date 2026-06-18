#include "app_storage.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app_storage";

esp_err_t app_storage_init(void) {
    // 1. Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash erase and re-init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash init failed: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Initialize LittleFS
    ESP_LOGI(TAG, "Mounting LittleFS...");
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(err));
        }
        return err;
    }

    size_t total = 0, used = 0;
    err = esp_littlefs_info(conf.partition_label, &total, &used);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}

const char* app_storage_get_path(void) {
    return "/littlefs";
}

esp_err_t app_storage_set_name(uint16_t id, const char *name) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("face_names", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    char key[16];
    snprintf(key, sizeof(key), "n_%u", id);
    err = nvs_set_str(handle, key, name);
    if (err == ESP_OK) nvs_commit(handle);
    
    nvs_close(handle);
    return err;
}

esp_err_t app_storage_get_name(uint16_t id, char *name_buf, size_t buf_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("face_names", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    char key[16];
    snprintf(key, sizeof(key), "n_%u", id);
    err = nvs_get_str(handle, key, name_buf, &buf_len);
    
    nvs_close(handle);
    return err;
}
