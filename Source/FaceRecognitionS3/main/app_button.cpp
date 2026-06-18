#include "app_button.h"
#include "app_face.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "app_button";

static void on_button_event(void *arg, void *usr_data)
{
    button_handle_t btn = (button_handle_t)arg;
    button_event_t event = iot_button_get_event(btn);

    switch (event) {
        case BUTTON_SINGLE_CLICK:
            ESP_LOGI(TAG, "Button: Single click -> Toggle Recognize");
            app_face_toggle_recognize();
            break;
        case BUTTON_DOUBLE_CLICK:
            ESP_LOGI(TAG, "Button: Double click -> Auto Enroll");
            app_face_enroll_auto();
            break;
        case BUTTON_LONG_PRESS_START:
            ESP_LOGI(TAG, "Button: Long press start -> Delete all faces");
            app_face_delete_all();
            break;
        default:
            break;
    }
}

void app_button_init(void)
{
    const button_config_t btn_cfg = {
        .long_press_time = 2000,
        .short_press_time = 180,
    };

    const button_gpio_config_t gpio_cfg = {
        .gpio_num = GPIO_NUM_0,
        .active_level = 0,
    };

    button_handle_t btn = NULL;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Button create failed: %s", esp_err_to_name(ret));
        return;
    }

    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, on_button_event, NULL);
    iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, NULL, on_button_event, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, on_button_event, NULL);

    ESP_LOGI(TAG, "BOOT Button initialized on GPIO 0");
}
