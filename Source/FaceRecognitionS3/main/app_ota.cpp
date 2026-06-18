#include "app_ota.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "app_web.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_ota";

static void ota_task(void *pvParameter) {
    char *url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting OTA update from %s", url);
    app_web_send_log_formatted("Starting OTA update from %s", url);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        free(url);
        vTaskDelete(NULL);
        return;
    }

    int total_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "Firmware size: %d bytes", total_size);

    int last_percent = -1;
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int read_so_far = esp_https_ota_get_image_len_read(ota_handle);
        if (total_size > 0) {
            int percent = (read_so_far * 100) / total_size;
            if (percent != last_percent) {
                last_percent = percent;
                ESP_LOGI(TAG, "OTA progress: %d%% (%d/%d bytes)", percent, read_so_far, total_size);
                app_web_send_log_formatted("OTA progress: %d%%", percent);
            }
        } else {
            static int last_printed_bytes = 0;
            if (read_so_far - last_printed_bytes >= 65536) {
                last_printed_bytes = read_so_far;
                ESP_LOGI(TAG, "OTA progress: %d bytes read", read_so_far);
            }
        }
    }

    if (err != ESP_OK) {
        esp_https_ota_abort(ota_handle);
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        app_web_send_log_formatted("OTA failed: %s", esp_err_to_name(err));
    } else {
        err = esp_https_ota_finish(ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA finished successfully. Rebooting...");
            app_web_send_log_formatted("OTA finished successfully. Rebooting device...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
            app_web_send_log_formatted("OTA finish failed: %s", esp_err_to_name(err));
        }
    }

    free(url);
    vTaskDelete(NULL);
}

esp_err_t app_ota_start(const char *url) {
    if (!url) return ESP_ERR_INVALID_ARG;
    
    char *url_copy = strdup(url);
    if (!url_copy) return ESP_ERR_NO_MEM;

    if (xTaskCreate(&ota_task, "ota_task", 8192, url_copy, 5, NULL) != pdPASS) {
        free(url_copy);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}
