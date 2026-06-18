#pragma once

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize AI pipeline (Face Detection & Recognition)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_face_init(void);

/**
 * @brief Main AI task to be run on Core 1
 * 
 * @param arg Task parameters
 */
void app_face_ai_task(void *arg);

/**
 * @brief Trigger enrollment for the next detected face
 * 
 * @param name The name to associate with this face
 */
void app_face_enroll(const char *name);

/**
 * @brief Delete all enrolled faces from the database
 */
void app_face_delete_all(void);

/**
 * @brief Toggle the recognize functionality (Detect only vs Detect+Recognize)
 */
void app_face_toggle_recognize(void);

/**
 * @brief Auto-enroll with a generated name like User_1
 */
void app_face_enroll_auto(void);

/**
 * @brief Get total number of enrolled faces
 * 
 * @return int Number of faces
 */
int app_face_get_count(void);

/**
 * @brief Draw Bounding boxes and Names onto a frame buffer (Async Overlay)
 * 
 * @param fb The camera frame buffer to draw on
 */
void app_face_draw_overlay(camera_fb_t *fb);

/**
 * @brief Draw a text string onto a frame buffer (RGB565 BE)
 * 
 * @param fb The camera frame buffer to draw on
 * @param x The starting X coordinate
 * @param y The starting Y coordinate
 * @param str The string to draw
 * @param color The RGB565 color (Big Endian)
 */
void app_face_draw_string(camera_fb_t *fb, int x, int y, const char *str, uint16_t color);

#ifdef __cplusplus
}
#endif
