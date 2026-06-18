#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start OTA update from a given URL (HTTP/HTTPS)
 * 
 * @param url The URL to download the firmware from.
 * @return esp_err_t ESP_OK if started successfully.
 */
esp_err_t app_ota_start(const char *url);

#ifdef __cplusplus
}
#endif
