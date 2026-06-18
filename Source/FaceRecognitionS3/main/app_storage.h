#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NVS and LittleFS storage
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_storage_init(void);

/**
 * @brief Get the base path for LittleFS
 * 
 * @return const char* "/littlefs"
 */
const char* app_storage_get_path(void);

/**
 * @brief Save a name for a specific Face ID in NVS
 */
esp_err_t app_storage_set_name(uint16_t id, const char *name);

/**
 * @brief Get a name for a specific Face ID from NVS
 */
esp_err_t app_storage_get_name(uint16_t id, char *name_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
