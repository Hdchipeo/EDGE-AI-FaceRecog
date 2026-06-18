#include "app_espnow.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "app_espnow";
static const uint8_t s_target_mac[6] = {0xe4, 0x65, 0xb8, 0x11, 0x7f, 0x9c};
static bool s_espnow_ready = false;

static void espnow_tx_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (tx_info && tx_info->des_addr) {
        ESP_LOGI(TAG, "ESP-NOW packet sent to " MACSTR ", status: %s", 
                 MAC2STR(tx_info->des_addr), status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
    } else {
        ESP_LOGI(TAG, "ESP-NOW packet sent, status: %s", 
                 status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
    }
}

esp_err_t app_espnow_init(void) {
    if (s_espnow_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing ESP-NOW...");
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_now_register_send_cb(espnow_tx_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register TX callback: %s", esp_err_to_name(err));
    }

    // Configure peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, s_target_mac, 6);
    peer_info.channel = 0; // Current channel
    peer_info.encrypt = false;
    peer_info.ifidx = WIFI_IF_STA; // Send on Station interface

    err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(err));
        esp_now_deinit();
        return err;
    }

    s_espnow_ready = true;
    ESP_LOGI(TAG, "ESP-NOW initialized. Peer " MACSTR " registered.", MAC2STR(s_target_mac));
    return ESP_OK;
}

esp_err_t app_espnow_send_light_on(void) {
    if (!s_espnow_ready) {
        esp_err_t err = app_espnow_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ESP-NOW not initialized yet, skipping LIGHT ON");
            return err;
        }
    }

    espnow_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = ESPNOW_TYPE_LED;
    pkt.led.lamp_id = 0;
    pkt.led.power = 1;       // ON
    pkt.led.brightness = 100; // Max brightness
    pkt.led.r = 255;
    pkt.led.g = 255;
    pkt.led.b = 255;

    ESP_LOGI(TAG, "Sending ESP-NOW LIGHT ON to " MACSTR, MAC2STR(s_target_mac));
    esp_err_t err = esp_now_send(s_target_mac, (const uint8_t *)&pkt, sizeof(pkt));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send ESP-NOW packet: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_espnow_send_light_off(void) {
    if (!s_espnow_ready) {
        esp_err_t err = app_espnow_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ESP-NOW not initialized yet, skipping LIGHT OFF");
            return err;
        }
    }

    espnow_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = ESPNOW_TYPE_LED;
    pkt.led.lamp_id = 0;
    pkt.led.power = 0;       // OFF

    ESP_LOGI(TAG, "Sending ESP-NOW LIGHT OFF to " MACSTR, MAC2STR(s_target_mac));
    esp_err_t err = esp_now_send(s_target_mac, (const uint8_t *)&pkt, sizeof(pkt));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send ESP-NOW packet: %s", esp_err_to_name(err));
    }
    return err;
}
