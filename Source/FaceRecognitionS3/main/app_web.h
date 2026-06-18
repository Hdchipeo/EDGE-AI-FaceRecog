#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Web Server for camera streaming and face management
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_web_start(void);

/**
 * @brief Stop the Web Server
 */
void app_web_stop(void);

/**
 * @brief Sync new enrolled face to Python server
 * 
 * @param id The face ID
 * @param name The face name
 */
void app_web_sync_enroll(int id, const char *name);

/**
 * @brief Sync new enrolled face and upload its portrait image to Python server
 * 
 * @param id The face ID
 * @param name The face name
 * @param jpg_buf Pointer to the compressed JPEG buffer
 * @param jpg_len Length of the JPEG buffer in bytes
 */
void app_web_sync_enroll_with_image(int id, const char *name, const uint8_t *jpg_buf, size_t jpg_len);

/**
 * @brief Send a startup webhook to the Python server to report the ESP32 IP address.
 * 
 * @param ip The local dynamic IP of the ESP32
 */
void app_web_send_startup_webhook(const char *ip);

/**
 * @brief Set the discovered Python server IP dynamically
 * 
 * @param ip The IP address of the Python server
 */
void app_web_set_server_ip(const char *ip);

/**
 * @brief Send a log message to the Python server
 * 
 * @param message The log message
 */
void app_web_send_log(const char *message);

/**
 * @brief Send a formatted log message to the Python server
 * 
 * @param format Format string
 * @param ... Arguments
 */
void app_web_send_log_formatted(const char *format, ...);

/**
 * @brief Initialize remote system logging (redirect esp_log to UDP)
 */
void app_log_init(void);

/**
 * @brief Set the destination server IP for UDP logs
 * 
 * @param server_ip The IP address of the Python server
 */
void app_log_set_server(const char *server_ip);

#ifdef __cplusplus
}
#endif
