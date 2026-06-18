#include "app_lcd.h"
#include "app_camera.h"
#include "app_face.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_lcd";

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_14
#define DISPLAY_MOSI_PIN GPIO_NUM_38
#define DISPLAY_CLK_PIN GPIO_NUM_47
#define DISPLAY_DC_PIN GPIO_NUM_39
#define DISPLAY_RST_PIN GPIO_NUM_40
#define DISPLAY_CS_PIN GPIO_NUM_41

static esp_lcd_panel_handle_t panel_handle = NULL;
static display_mode_t current_mode = DISPLAY_MODE_LCD; // Force LCD mode at boot
static TaskHandle_t lcd_task_handle = NULL;

static bool lcd_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *edata,
                              void *user_ctx) {
  TaskHandle_t *task_ptr = (TaskHandle_t *)user_ctx;
  if (*task_ptr == NULL)
    return false;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(*task_ptr, &xHigherPriorityTaskWoken);
  return xHigherPriorityTaskWoken == pdTRUE;
}

static void lcd_task(void *pvParameters) {
  lcd_task_handle = xTaskGetCurrentTaskHandle();
  ESP_LOGI(TAG, "LCD Task Started");
  int64_t last_time = 0;
  uint32_t frame_count = 0;
  float fps = 0.0f;
  while (1) {
    if (current_mode == DISPLAY_MODE_LCD) {
      camera_fb_t *fb = app_camera_capture();
      if (fb) {
        // Overlay latest AI results
        app_face_draw_overlay(fb);

        // Measure FPS
        int64_t now = esp_timer_get_time();
        frame_count++;
        if (last_time == 0) {
          last_time = now;
        } else {
          int64_t diff = now - last_time;
          if (diff >= 1000000) { // Update every 1 second
            fps = (float)frame_count * 1000000.0f / diff;
            frame_count = 0;
            last_time = now;
            ESP_LOGI(TAG, "LCD FPS: %.2f", fps);
          }
        }

        // Draw FPS on screen if calculated
        if (fps > 0.0f) {
          char fps_str[16];
          snprintf(fps_str, sizeof(fps_str), "FPS: %.1f", fps);
          app_face_draw_string(fb, 10, 10, fps_str, 0xE007); // Green BE
        }

        // Draw to LCD in chunks to avoid SPI max_transfer_sz limits
        if (panel_handle) {
          const int chunk_lines = 40;
          int chunks_queued = 0;
          for (int y = 0; y < fb->height; y += chunk_lines) {
            int lines =
                (y + chunk_lines > fb->height) ? (fb->height - y) : chunk_lines;
            esp_err_t err = esp_lcd_panel_draw_bitmap(
                panel_handle, 0, y, fb->width, y + lines,
                fb->buf + y * fb->width * 2);
            if (err == ESP_OK) {
              chunks_queued++;
            } else {
              ESP_LOGE(TAG, "Failed to draw bitmap at y=%d, err=0x%x", y, err);
            }
          }
          // Wait for all chunks to finish DMA transfer before releasing the
          // buffer
          for (int i = 0; i < chunks_queued; i++) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
          }
        }

        app_camera_release(fb);
      } else {
        ESP_LOGE(TAG, "Failed to capture frame from camera");
      }
      vTaskDelay(pdMS_TO_TICKS(5)); // Reduced delay to increase FPS
    } else {
      vTaskDelay(pdMS_TO_TICKS(100)); // Sleep when not in LCD mode
    }
  }
}

esp_err_t app_lcd_init(void) {
  ESP_LOGI(TAG, "Initialize SPI bus");
  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = DISPLAY_CLK_PIN;
  buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 320 * 40 * 2 + 8; // Match chunk size (40 lines)

  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

  ESP_LOGI(TAG, "Install panel IO");
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = DISPLAY_DC_PIN;
  io_config.cs_gpio_num = DISPLAY_CS_PIN;
  io_config.pclk_hz = 60 * 1000 * 1000; // Increased to 60MHz for better FPS
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 10;
  io_config.on_color_trans_done = lcd_trans_done_cb;
  io_config.user_ctx = &lcd_task_handle;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                           &io_config, &io_handle));

  ESP_LOGI(TAG, "Install ST7789 panel driver");
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = DISPLAY_RST_PIN;
  panel_config.rgb_endian = LCD_RGB_ENDIAN_RGB;
  panel_config.bits_per_pixel = 16;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

  // ST7789 typical requirements
  ESP_ERROR_CHECK(
      esp_lcd_panel_invert_color(panel_handle, false)); // Fixed negative effect

  // If the camera is rotated 90 or 270 degrees, the frame size is portrait (240x320).
  // The panel is natively 240x320, so we do not swap xy in those cases.
#if CONFIG_CAMERA_ROTATION_90 || CONFIG_CAMERA_ROTATION_270
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
#else
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
#endif

  // Turn on display
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  ESP_LOGI(TAG, "Turn on backlight");
  gpio_set_direction(DISPLAY_BACKLIGHT_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1);

  // Start LCD task with higher priority than AI task
  xTaskCreatePinnedToCore(lcd_task, "lcd_task", 4096, NULL, 6, NULL, 1);

  return ESP_OK;
}

void app_lcd_set_mode(display_mode_t mode) {
  current_mode = mode;
  ESP_LOGI(TAG, "Display mode switched to %s",
           mode == DISPLAY_MODE_LCD ? "LCD" : "WEB");
}

display_mode_t app_lcd_get_mode(void) { return current_mode; }
