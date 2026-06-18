#pragma once

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Camera configuration
#define CAMERA_FRAME_BUFFER_COUNT 3
#define CAMERA_FRAME_WIDTH 320
#define CAMERA_FRAME_HEIGHT 240

/**
 * @brief Initialize camera with static memory pool
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_camera_init(void);

/**
 * @brief Capture a frame from the camera
 *
 * @return camera_fb_t* Pointer to frame buffer, NULL on failure
 */
camera_fb_t *app_camera_capture(void);

/**
 * @brief Release a captured frame buffer
 *
 * @param fb Pointer to frame buffer to return
 */
void app_camera_release(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif
