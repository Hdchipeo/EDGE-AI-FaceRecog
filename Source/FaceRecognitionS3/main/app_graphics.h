#pragma once

#include <stdint.h>
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draw a hollow rectangle onto an RGB565 frame buffer.
 * 
 * @param buf Frame buffer pointer
 * @param w Width of the buffer
 * @param h Height of the buffer
 * @param x1 Starting X coordinate
 * @param y1 Starting Y coordinate
 * @param x2 Ending X coordinate
 * @param y2 Ending Y coordinate
 * @param color RGB565 color (Big Endian)
 */
void draw_rect(uint16_t *buf, int w, int h, int x1, int y1, int x2, int y2, uint16_t color);

/**
 * @brief Draw a single character onto an RGB565 frame buffer.
 * 
 * @param buf Frame buffer pointer
 * @param w Width of the buffer
 * @param h Height of the buffer
 * @param x Starting X coordinate
 * @param y Starting Y coordinate
 * @param c The character to draw (ASCII 32-127)
 * @param color RGB565 color (Big Endian)
 */
void draw_char(uint16_t *buf, int w, int h, int x, int y, char c, uint16_t color);

/**
 * @brief Draw a string of text onto an RGB565 frame buffer.
 * 
 * @param buf Frame buffer pointer
 * @param w Width of the buffer
 * @param h Height of the buffer
 * @param x Starting X coordinate
 * @param y Starting Y coordinate
 * @param str The string of characters to draw
 * @param color RGB565 color (Big Endian)
 */
void draw_string(uint16_t *buf, int w, int h, int x, int y, const char *str, uint16_t color);

#ifdef __cplusplus
}
#endif
