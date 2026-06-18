#include "app_camera.h"
#include "esp_log.h"

static const char *TAG = "app_camera";

#if CONFIG_CAMERA_ROTATION_90 || CONFIG_CAMERA_ROTATION_180 || CONFIG_CAMERA_ROTATION_270
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ROTATION_POOL_SIZE 4

typedef struct {
    camera_fb_t *fb;
    uint8_t *original_buf;
    uint8_t *rotated_buf;
    size_t original_width;
    size_t original_height;
} rotation_mapping_t;

static rotation_mapping_t rotation_pool[ROTATION_POOL_SIZE];
static portMUX_TYPE pool_mux = portMUX_INITIALIZER_UNLOCKED;

static void rotate_rgb565_90_cw(const uint16_t *src, uint16_t *dst, int w, int h) {
    for (int x = 0; x < w; x++) {
        uint16_t *dst_row = &dst[x * h];
        for (int y = 0; y < h; y++) {
            dst_row[y] = src[(h - 1 - y) * w + x];
        }
    }
}

static void rotate_rgb565_90_ccw(const uint16_t *src, uint16_t *dst, int w, int h) {
    for (int x = 0; x < w; x++) {
        uint16_t *dst_row = &dst[x * h];
        for (int y = 0; y < h; y++) {
            dst_row[y] = src[y * w + (w - 1 - x)];
        }
    }
}

static void rotate_rgb565_180(const uint16_t *src, uint16_t *dst, int w, int h) {
    int total_pixels = w * h;
    for (int i = 0; i < total_pixels; i++) {
        dst[total_pixels - 1 - i] = src[i];
    }
}
#endif

// Common ESP32-S3-WROOM-1 Camera Pinout (e.g. Freenove/ESP-EYE)
// USER: Please verify these pins for your specific board!
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5

#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

esp_err_t app_camera_init(void) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 12000000; // Lowered to 12MHz for stability
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_RGB565; // Optimized for AI inference
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = CAMERA_FRAME_BUFFER_COUNT;

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
    return err;
  }

#if CONFIG_CAMERA_ROTATION_90 || CONFIG_CAMERA_ROTATION_180 || CONFIG_CAMERA_ROTATION_270
  for (int i = 0; i < ROTATION_POOL_SIZE; i++) {
    rotation_pool[i].rotated_buf = (uint8_t *)heap_caps_malloc(CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    if (!rotation_pool[i].rotated_buf) {
      ESP_LOGE(TAG, "Failed to allocate rotated buffer %d in PSRAM", i);
      return ESP_ERR_NO_MEM;
    }
    rotation_pool[i].fb = NULL;
  }
  ESP_LOGI(TAG, "Rotation pool initialized with %d buffers in PSRAM", ROTATION_POOL_SIZE);
#endif

  sensor_t *s = esp_camera_sensor_get();
  s->set_pixformat(s, PIXFORMAT_RGB565); // Redundant set to fix driver
                                         // initialization edge cases

  // Rotate camera 90 degrees
  s->set_vflip(s, 0);
  s->set_hmirror(s, 0);

  if (s->id.PID == OV3660_PID) {
    s->set_brightness(s, 1);  // Up the brightness just a bit
    s->set_saturation(s, -2); // Lower the saturation
  }

  ESP_LOGI(TAG, "Camera initialized successfully (OV3660)");
  return ESP_OK;
}

camera_fb_t *app_camera_capture(void) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return NULL;

#if CONFIG_CAMERA_ROTATION_90 || CONFIG_CAMERA_ROTATION_180 || CONFIG_CAMERA_ROTATION_270
  if (fb->format != PIXFORMAT_RGB565) {
    return fb;
  }

  int slot_idx = -1;
  portENTER_CRITICAL(&pool_mux);
  for (int i = 0; i < ROTATION_POOL_SIZE; i++) {
    if (rotation_pool[i].fb == NULL) {
      rotation_pool[i].fb = fb;
      slot_idx = i;
      break;
    }
  }
  portEXIT_CRITICAL(&pool_mux);

  if (slot_idx == -1) {
    ESP_LOGE(TAG, "Rotation pool full! Returning unrotated frame");
    return fb;
  }

  rotation_pool[slot_idx].original_buf = fb->buf;
  rotation_pool[slot_idx].original_width = fb->width;
  rotation_pool[slot_idx].original_height = fb->height;

#if CONFIG_CAMERA_ROTATION_90
  rotate_rgb565_90_cw((const uint16_t *)fb->buf, (uint16_t *)rotation_pool[slot_idx].rotated_buf, fb->width, fb->height);
  fb->buf = rotation_pool[slot_idx].rotated_buf;
  fb->width = rotation_pool[slot_idx].original_height;
  fb->height = rotation_pool[slot_idx].original_width;
#elif CONFIG_CAMERA_ROTATION_270
  rotate_rgb565_90_ccw((const uint16_t *)fb->buf, (uint16_t *)rotation_pool[slot_idx].rotated_buf, fb->width, fb->height);
  fb->buf = rotation_pool[slot_idx].rotated_buf;
  fb->width = rotation_pool[slot_idx].original_height;
  fb->height = rotation_pool[slot_idx].original_width;
#elif CONFIG_CAMERA_ROTATION_180
  rotate_rgb565_180((const uint16_t *)fb->buf, (uint16_t *)rotation_pool[slot_idx].rotated_buf, fb->width, fb->height);
  fb->buf = rotation_pool[slot_idx].rotated_buf;
#endif

#endif
  return fb;
}

void app_camera_release(camera_fb_t *fb) {
  if (!fb) return;

#if CONFIG_CAMERA_ROTATION_90 || CONFIG_CAMERA_ROTATION_180 || CONFIG_CAMERA_ROTATION_270
  int slot_idx = -1;
  portENTER_CRITICAL(&pool_mux);
  for (int i = 0; i < ROTATION_POOL_SIZE; i++) {
    if (rotation_pool[i].fb == fb) {
      slot_idx = i;
      break;
    }
  }
  portEXIT_CRITICAL(&pool_mux);

  if (slot_idx != -1) {
    fb->buf = rotation_pool[slot_idx].original_buf;
    fb->width = rotation_pool[slot_idx].original_width;
    fb->height = rotation_pool[slot_idx].original_height;

    portENTER_CRITICAL(&pool_mux);
    rotation_pool[slot_idx].fb = NULL;
    portEXIT_CRITICAL(&pool_mux);
  }
#endif

  esp_camera_fb_return(fb);
}

