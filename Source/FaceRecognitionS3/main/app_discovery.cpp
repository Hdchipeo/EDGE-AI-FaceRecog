#include "app_discovery.h"
#include "app_web.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

static const char *TAG = "app_discovery";
static char s_my_ip[16] = {0};
static TaskHandle_t s_discovery_task_handle = NULL;

static void udp_discovery_task(void *pvParameters) {
    char rx_buffer[128];
    char tx_buffer[64];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8090);

    snprintf(tx_buffer, sizeof(tx_buffer), "ESP32_DISCOVER:%s", s_my_ip);

    while (1) {
        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int broadcast = 1;
        int err = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        if (err < 0) {
            ESP_LOGE(TAG, "Failed to set SO_BROADCAST: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Bind to local port 8090 to receive the reply
        struct sockaddr_in local_addr;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(8090);
        err = bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Sending discovery broadcast: '%s'", tx_buffer);
        int len = sendto(sock, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        } else {
            struct sockaddr_storage source_addr;
            socklen_t socklen = sizeof(source_addr);
            int rx_len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            if (rx_len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ESP_LOGD(TAG, "Receive timeout, retrying...");
                } else {
                    ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                }
            } else {
                rx_buffer[rx_len] = '\0';
                ESP_LOGI(TAG, "Received response: '%s'", rx_buffer);
                if (strncmp(rx_buffer, "SERVER_IP:", 10) == 0) {
                    const char *server_ip = rx_buffer + 10;
                    ESP_LOGI(TAG, "Discovered Python Server at: %s", server_ip);
                    app_web_set_server_ip(server_ip);
                    
                    // Send startup webhook as final confirmation
                    app_web_send_startup_webhook(s_my_ip);
                    
                    close(sock);
                    ESP_LOGI(TAG, "UDP Discovery task shutting down successfully.");
                    s_discovery_task_handle = NULL;
                    vTaskDelete(NULL);
                }
            }
        }
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_discovery_start(const char *my_ip) {
    strncpy(s_my_ip, my_ip, sizeof(s_my_ip) - 1);
    if (s_discovery_task_handle == NULL) {
        xTaskCreate(udp_discovery_task, "udp_discovery", 4096, NULL, 5, &s_discovery_task_handle);
    }
}
