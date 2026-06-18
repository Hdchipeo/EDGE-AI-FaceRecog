#include "app_web.h"
#include "app_camera.h"
#include "app_face.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "app_lcd.h"
#include "app_storage.h"
#include "app_ota.h"
#include "cJSON.h"

static const char *TAG = "app_web";

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t ctrl_server = NULL;
static httpd_handle_t stream_server = NULL;
static char s_python_server_ip[32] = CONFIG_PYTHON_SERVER_IP;

static esp_err_t set_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK) return res;

    ESP_LOGI(TAG, "Web stream started (High-Speed Async Mode)");

    while(true){
        if (app_lcd_get_mode() == DISPLAY_MODE_LCD) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Yield to LCD task
            continue;
        }

        // Capture frame via wrapper (Non-blocking)
        fb = app_camera_capture();
        if (fb) {
            // Overlay latest AI results
            app_face_draw_overlay(fb);

            // Convert to JPEG
            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                if(!jpeg_converted) res = ESP_FAIL;
            } else {
                _jpg_buf_len = fb->len; _jpg_buf = fb->buf;
            }

            if(res == ESP_OK){
                size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
            }
            if(res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
            if(res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

            if(_jpg_buf && fb->format != PIXFORMAT_JPEG) { free(_jpg_buf); _jpg_buf = NULL; }
            app_camera_release(fb);
            fb = NULL;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if(res != ESP_OK) break;
    }
    return res;
}

static esp_err_t mode_handler(httpd_req_t *req) {
    set_cors_headers(req);
    char query[64];
    char mode[16] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "set", mode, sizeof(mode));
    }
    if (strcmp(mode, "lcd") == 0) {
        app_lcd_set_mode(DISPLAY_MODE_LCD);
        return httpd_resp_send(req, "Switched to LCD Mode", HTTPD_RESP_USE_STRLEN);
    } else if (strcmp(mode, "web") == 0) {
        app_lcd_set_mode(DISPLAY_MODE_WEB);
        return httpd_resp_send(req, "Switched to Web Mode", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "Invalid mode", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t index_handler(httpd_req_t *req) {
    const char* html = 
        "<!DOCTYPE html><html><head><title>Face Recog S3</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>body{text-align:center; font-family:sans-serif; background:#111; color:#eee;}"
        "img{border:3px solid #444; border-radius:8px; width:100%; max-width:640px; background:#000;}"
        ".config{margin:20px auto; max-width:400px; background:#222; padding:20px; border-radius:12px;}"
        "input{padding:10px; width:80%; margin-bottom:10px; border-radius:4px; border:1px solid #444; background:#333; color:white;}"
        ".btn{padding:12px 24px; margin:5px; border:none; border-radius:8px; cursor:pointer; font-weight:bold;}"
        ".enroll{background:#2ecc71; color:white; width:90%;}"
        ".delete{background:#e74c3c; color:white; font-size:12px; margin-top:20px;}"
        "#msg{margin-top:10px; color:#3498db;}</style></head>"
        "<body><h2>AI Face Identification</h2>"
        "<img id='stream' src=''>"
        "<div class='config'>"
        "  <input type='text' id='name' placeholder='Enter Name to Enroll'>"
        "  <button class='btn enroll' onclick='enroll()'>Enroll New Person</button>"
        "  <div id='msg'></div>"
        "  <button class='btn delete' onclick='cmd(\"/delete\")'>Clear All Data</button>"
        "  <div style='margin-top:20px;'><button class='btn' onclick='cmd(\"/mode?set=web\")' style='background:#3498db; color:white; font-size:14px;'>Web Mode</button>"
        "  <button class='btn' onclick='cmd(\"/mode?set=lcd\")' style='background:#f39c12; color:white; font-size:14px;'>LCD Mode</button></div>"
        "</div>"
        "<script>"
        "window.onload=()=>{ document.getElementById('stream').src='http://'+window.location.hostname+':81/stream'; };"
        "function enroll(){ "
        "  var n = document.getElementById('name').value;"
        "  if(!n){ alert('Please enter a name'); return; }"
        "  cmd('/enroll?name=' + encodeURIComponent(n));"
        "}"
        "function cmd(u){ "
        "  document.getElementById('msg').innerText='Processing...';"
        "  fetch(u).then(r=>r.text()).then(t=>{ document.getElementById('msg').innerText=t; });"
        "}</script></body></html>";
    return httpd_resp_send(req, html, strlen(html));
}

static esp_err_t enroll_handler(httpd_req_t *req) {
    set_cors_headers(req);
    char query[64];
    char name[32] = "User";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "name", name, sizeof(name));
    }
    app_face_enroll(name);
    return httpd_resp_send(req, "Enrollment Started...", 21);
}

static esp_err_t delete_handler(httpd_req_t *req) {
    set_cors_headers(req);
    app_face_delete_all();
    return httpd_resp_send(req, "Database Cleared", 16);
}

void app_web_sync_enroll(int id, const char *name) {
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"id\":%d, \"name\":\"%s\"}", id, name);
    
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/webhook/sync", s_python_server_ip, CONFIG_PYTHON_SERVER_PORT);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Webhook sync success");
    } else {
        ESP_LOGE(TAG, "Webhook sync failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void app_web_sync_enroll_with_image(int id, const char *name, const uint8_t *jpg_buf, size_t jpg_len) {
    // 1. Sync name and ID first
    app_web_sync_enroll(id, name);
    
    if (jpg_buf == NULL || jpg_len == 0) {
        ESP_LOGE(TAG, "No image buffer to upload");
        return;
    }

    // 2. Upload image
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/webhook/upload_image?id=%d", s_python_server_ip, CONFIG_PYTHON_SERVER_PORT, id);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for image upload");
        return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    esp_http_client_set_post_field(client, (const char *)jpg_buf, jpg_len);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Image upload success");
    } else {
        ESP_LOGE(TAG, "Image upload failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void app_web_send_startup_webhook(const char *ip) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"ip\":\"%s\"}", ip);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/webhook/startup", s_python_server_ip, CONFIG_PYTHON_SERVER_PORT);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Startup webhook success");
    } else {
        ESP_LOGE(TAG, "Startup webhook failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void app_web_set_server_ip(const char *ip) {
    strncpy(s_python_server_ip, ip, sizeof(s_python_server_ip) - 1);
    s_python_server_ip[sizeof(s_python_server_ip) - 1] = '\0';
    ESP_LOGI(TAG, "Python server IP updated dynamically to: %s", s_python_server_ip);
    app_log_set_server(ip);
}

static esp_err_t api_faces_get_handler(httpd_req_t *req) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    int count = app_face_get_count();
    httpd_resp_send_chunk(req, "[", 1);
    char buf[128];
    for (int i = 1; i <= count; i++) {
        char name[32] = "Unknown";
        app_storage_get_name(i, name, sizeof(name));
        int len = snprintf(buf, sizeof(buf), "{\"id\":%d,\"name\":\"%s\"}%s", i, name, (i < count) ? "," : "");
        httpd_resp_send_chunk(req, buf, len);
    }
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_faces_delete_handler(httpd_req_t *req) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    app_face_delete_all();
    return httpd_resp_send(req, "{\"status\":\"ok\"}", 15);
}

static esp_err_t api_ota_handler(httpd_req_t *req) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    
    char *url_start = strstr(buf, "http");
    if (!url_start) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing URL");
        return ESP_FAIL;
    }
    char *url_end = strchr(url_start, '"');
    if (url_end) *url_end = '\0';
    
    ESP_LOGI(TAG, "OTA Trigger received URL: %s", url_start);
    app_ota_start(url_start);
    
    return httpd_resp_send(req, "{\"status\":\"ota_started\"}", 24);
}

static esp_err_t api_faces_rename_handler(httpd_req_t *req) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty payload");
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    cJSON *name_item = cJSON_GetObjectItem(root, "name");
    
    if (!id_item || !cJSON_IsNumber(id_item) || !name_item || !cJSON_IsString(name_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing/Invalid fields");
        return ESP_FAIL;
    }
    
    int id = id_item->valueint;
    const char *name = name_item->valuestring;
    
    ESP_LOGI(TAG, "Rename request: ID %d -> %s", id, name);
    esp_err_t err = app_storage_set_name(id, name);
    cJSON_Delete(root);
    
    if (err == ESP_OK) {
        return httpd_resp_send(req, "{\"status\":\"ok\"}", 15);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save to NVS");
        return ESP_FAIL;
    }
}

esp_err_t app_web_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    if (httpd_start(&ctrl_server, &config) == ESP_OK) {
        httpd_uri_t index_uri = { "/", HTTP_GET, index_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &index_uri);
        httpd_uri_t enroll_uri = { "/enroll", HTTP_GET, enroll_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &enroll_uri);
        httpd_uri_t delete_uri = { "/delete", HTTP_GET, delete_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &delete_uri);
        httpd_uri_t mode_uri = { "/mode", HTTP_GET, mode_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &mode_uri);
        
        httpd_uri_t api_faces_get = { "/api/faces", HTTP_GET, api_faces_get_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &api_faces_get);
        
        httpd_uri_t api_faces_del = { "/api/faces", HTTP_DELETE, api_faces_delete_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &api_faces_del);
        
        httpd_uri_t api_faces_rename = { "/api/faces/rename", HTTP_POST, api_faces_rename_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &api_faces_rename);
        
        httpd_uri_t api_ota = { "/api/ota_trigger", HTTP_POST, api_ota_handler, NULL };
        httpd_register_uri_handler(ctrl_server, &api_ota);
    }
    config.server_port = 81;
    config.ctrl_port = 32769;
    if (httpd_start(&stream_server, &config) == ESP_OK) {
        httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
        httpd_register_uri_handler(stream_server, &stream_uri);
    }
    return ESP_OK;
}

void app_web_stop(void) {
    if (ctrl_server) httpd_stop(ctrl_server);
    if (stream_server) httpd_stop(stream_server);
}

#include <stdarg.h>

void app_web_send_log(const char *message) {
    if (s_python_server_ip[0] == '\0') {
        return;
    }
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"message\":\"%s\"}", message);
    
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/webhook/log", s_python_server_ip, CONFIG_PYTHON_SERVER_PORT);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 2000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Log webhook failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void app_web_send_log_formatted(const char *format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    
    ESP_LOGI("ESP_LOG_SERVER", "%s", buf);
    app_web_send_log(buf);
}

#include "esp_log.h"
#include "lwip/sockets.h"
#include <fcntl.h>

static vprintf_like_t s_default_log_vprintf = NULL;
static int s_log_udp_sock = -1;
static struct sockaddr_in s_server_log_addr;
static bool s_server_log_ready = false;

static int custom_log_vprintf(const char *format, va_list args) {
    int written = 0;
    if (s_default_log_vprintf) {
        written = s_default_log_vprintf(format, args);
    } else {
        written = vprintf(format, args);
    }

    if (s_server_log_ready && s_log_udp_sock >= 0) {
        char log_buf[256];
        va_list args_copy;
        va_copy(args_copy, args);
        int log_len = vsnprintf(log_buf, sizeof(log_buf), format, args_copy);
        va_end(args_copy);

        if (log_len > 0) {
            sendto(s_log_udp_sock, log_buf, log_len, 0, 
                   (struct sockaddr *)&s_server_log_addr, sizeof(s_server_log_addr));
        }
    }
    return written;
}

void app_log_init(void) {
    s_default_log_vprintf = esp_log_set_vprintf(custom_log_vprintf);
}

void app_log_set_server(const char *server_ip) {
    if (s_log_udp_sock >= 0) {
        close(s_log_udp_sock);
        s_log_udp_sock = -1;
    }

    s_log_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_log_udp_sock < 0) {
        ESP_LOGE("app_log", "Failed to create log UDP socket");
        s_server_log_ready = false;
        return;
    }

    int flags = fcntl(s_log_udp_sock, F_GETFL, 0);
    fcntl(s_log_udp_sock, F_SETFL, flags | O_NONBLOCK);

    memset(&s_server_log_addr, 0, sizeof(s_server_log_addr));
    s_server_log_addr.sin_addr.s_addr = inet_addr(server_ip);
    s_server_log_addr.sin_family = AF_INET;
    s_server_log_addr.sin_port = htons(8091);
    s_server_log_ready = true;
    
    ESP_LOGI("app_log", "Remote UDP logging configured to %s:8091", server_ip);
}
