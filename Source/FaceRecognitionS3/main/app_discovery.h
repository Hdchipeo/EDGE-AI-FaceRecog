#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the UDP discovery task to dynamically discover the python server IP
 * 
 * @param my_ip The dynamic IP of the ESP32 to report in the discover packet
 */
void app_discovery_start(const char *my_ip);

#ifdef __cplusplus
}
#endif
