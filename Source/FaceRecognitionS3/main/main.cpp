#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "app_camera.h"
#include "app_storage.h"
#include "app_face.h"
#include "app_web.h"
#include "app_espnow.h"

#if CONFIG_ENABLE_LCD
#include "app_lcd.h"
#endif

#if CONFIG_ENABLE_CONSOLE
#include "app_console.h"
#endif

#include "app_button.h"
#include "app_discovery.h"

static const char *TAG = "main";

// Queue for camera frames to AI Task (Asynchronous)
QueueHandle_t xQueueFrame = NULL;

static bool s_web_server_started = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Station started. Connecting to AP...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected. Retrying connection...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
        
        if (!s_web_server_started) {
            ESP_ERROR_CHECK(app_web_start());
            s_web_server_started = true;
        }
        
        // Start UDP discovery to report IP and locate python server
        app_discovery_start(ip_str);
    }
}

extern "C" void app_main(void)
{
    // 0. Cancel OTA rollback (mark app as valid)
    esp_err_t ota_err = esp_ota_mark_app_valid_cancel_rollback();
    if (ota_err == ESP_OK) {
        ESP_LOGI(TAG, "OTA App validated successfully. Rollback canceled.");
    } else if (ota_err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        ESP_LOGD(TAG, "OTA App state is not pending validation.");
    } else {
        ESP_LOGE(TAG, "Failed to cancel OTA rollback: %s", esp_err_to_name(ota_err));
    }

    // Initialize remote system log redirection
    app_log_init();

    ESP_LOGI(TAG, "App version 1.0.2");

    ESP_LOGI(TAG, "Starting Face Recognition System (Optimized for FPS)...");

    // 1. Basic Init
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Storage
    ESP_ERROR_CHECK(app_storage_init());

    // 3. Camera (QVGA RGB565)
    ESP_ERROR_CHECK(app_camera_init());

    // 3.5. LCD Display
#if CONFIG_ENABLE_LCD
    ESP_ERROR_CHECK(app_lcd_init());
#endif

    // 4. AI System
    ESP_ERROR_CHECK(app_face_init());

    // 5. WiFi STA & Event Handlers
#if CONFIG_ENABLE_WEB_SERVER
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_ESP_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_ESP_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize ESP-NOW
    app_espnow_init();
#else
    // If Web Server is disabled, minimal Wi-Fi initialization is still required for ESP-NOW
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize ESP-NOW
    app_espnow_init();
#endif

#if CONFIG_ENABLE_CONSOLE
    // 6.5 Console Init
    app_console_init();
#endif

    // 7. Initialize AI Dispatcher
    xQueueFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xTaskCreatePinnedToCore(app_face_ai_task, "ai_task", 32 * 1024, NULL, 5, NULL, 1);

    // 7.5 Button
    app_button_init();

    // 8. Capture Loop for AI
    // Note: The Web Stream handles its own capture for maximum FPS
    ESP_LOGI(TAG, "AI Dispatcher started on Core 0...");
    while (1) {
        camera_fb_t *fb = app_camera_capture();
        if (fb) {
            // Push to AI task
            if (xQueueSend(xQueueFrame, &fb, 0) != pdTRUE) {
                app_camera_release(fb);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Feed AI at ~20Hz max to save PSRAM bandwidth
    }
}
