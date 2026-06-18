#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Message type definition (matches NexusIR LED control)
#define ESPNOW_TYPE_LED 1

// espnow_packet_t structure matching exactly NexusIR's definition
typedef struct {
  uint8_t type; // 0: AC, 1: LED, 2: FAN, 3: TEMP
  union {
    struct {
      uint8_t power;
      uint8_t mode;
      uint8_t temp;
      uint8_t fan;
      uint8_t brand;
      char custom_name[32];
    } ac;
    struct {
      uint8_t lamp_id;
      uint8_t power;
      uint8_t effect;
      uint8_t brightness;
      uint8_t r;
      uint8_t g;
      uint8_t b;
      uint8_t speed;
    } led;
    struct {
      uint8_t power;
      uint8_t speed;
      uint8_t swing;
      uint8_t brand;
      char custom_name[32];
    } fan;
    struct {
      int16_t temp_x10;   // °C × 10
      int16_t humid_x10;  // % × 10
    } temp;
    struct {
      uint8_t idx;
      uint8_t state;
    } relay;
    struct {
      char device_name[32];
    } discovery;
  };
} espnow_packet_t;

/**
 * @brief Initialize ESP-NOW on the device
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_init(void);

/**
 * @brief Send command to turn the light ON via ESP-NOW
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_send_light_on(void);

/**
 * @brief Send command to turn the light OFF via ESP-NOW
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_send_light_off(void);

#ifdef __cplusplus
}
#endif
