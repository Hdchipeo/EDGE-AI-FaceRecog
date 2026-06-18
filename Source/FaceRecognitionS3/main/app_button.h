#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the BOOT button (GPIO 0) and register callbacks.
 */
void app_button_init(void);

#ifdef __cplusplus
}
#endif
