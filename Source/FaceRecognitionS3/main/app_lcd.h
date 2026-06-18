#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_MODE_WEB,
    DISPLAY_MODE_LCD
} display_mode_t;

/**
 * @brief Initialize ST7789 LCD display via SPI
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_lcd_init(void);

/**
 * @brief Set the current display mode
 * 
 * @param mode Mode to set (WEB or LCD)
 */
void app_lcd_set_mode(display_mode_t mode);

/**
 * @brief Get the current display mode
 * 
 * @return display_mode_t Current mode
 */
display_mode_t app_lcd_get_mode(void);

#ifdef __cplusplus
}
#endif
