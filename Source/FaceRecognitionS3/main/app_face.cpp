#include "app_face.h"
#include "app_camera.h"
#include "app_espnow.h"
#include "app_storage.h"
#include "app_graphics.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "dl_image_process.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <vector>
#include <cmath>
#include "sdkconfig.h"
#include "esp_task_wdt.h"
#include "dl_recognition_define.hpp"
#include "img_converters.h"

static const char *TAG = "app_face";

#define FACE_RECOGNITION_THRESHOLD 0.70f
#define FACE_STRANGER_THRESHOLD    0.50f

static void app_face_check_db_compatibility(const char *db_path, int expected_len) {
    FILE *f = fopen(db_path, "rb");
    if (f) {
        dl::recognition::database_meta meta;
        if (fread(&meta, sizeof(dl::recognition::database_meta), 1, f) == 1) {
            if (meta.feat_len != expected_len) {
                ESP_LOGW("app_face", "DB length mismatch (stored: %d, model: %d). Wiping old DB.", meta.feat_len, expected_len);
                fclose(f);
                remove(db_path);
                f = nullptr;
            }
        }
        if (f) fclose(f);
    }
}

#if CONFIG_MODEL_TYPE_CUSTOM_MOBILENETV2
extern const uint8_t mobilenetv2_espdl_start[] asm("_binary_mobilenetv2_128d_v3_espdl_start");

static void model_load_task(void *pvParameters);

class CustomFeat : public dl::feat::FeatImpl {
public:
    bool m_ready = false;
    CustomFeat() {
        xTaskCreatePinnedToCore(model_load_task, "model_load", 32 * 1024, this, tskIDLE_PRIORITY, NULL, 1);
        while (!m_ready) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    friend void model_load_task(void *pvParameters);
    
    void set_model(dl::Model *model) { m_model = model; }
    void set_preprocessor(dl::image::FeatImagePreprocessor *pre) { m_image_preprocessor = pre; }
    void set_postprocessor(dl::feat::FeatPostprocessor *post) { m_postprocessor = post; }
};

static void model_load_task(void *pvParameters) {
    CustomFeat *feat = (CustomFeat *)pvParameters;
    ESP_LOGI("CustomFeat", "Loading model at %p...", mobilenetv2_espdl_start);
    if (mobilenetv2_espdl_start[0] == 0x45 && mobilenetv2_espdl_start[1] == 0x44 && 
        mobilenetv2_espdl_start[2] == 0x4c && mobilenetv2_espdl_start[3] == 0x32) {
        ESP_LOGI("CustomFeat", "Model magic header OK: EDL2");
    } else {
        ESP_LOGE("CustomFeat", "Model magic header WRONG: %02x %02x %02x %02x", 
                 mobilenetv2_espdl_start[0], mobilenetv2_espdl_start[1], 
                 mobilenetv2_espdl_start[2], mobilenetv2_espdl_start[3]);
    }
    
    dl::Model *model = new dl::Model((const char *)mobilenetv2_espdl_start, fbs::MODEL_LOCATION_IN_FLASH_RODATA, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);
    feat->set_model(model);
    ESP_LOGI("CustomFeat", "Model created at %p", model);
    
    // Debug: Print inputs
    auto inputs = model->get_inputs();
    ESP_LOGI("CustomFeat", "Model has %d inputs:", (int)inputs.size());
    for (auto const& [name, tensor] : inputs) {
        ESP_LOGI("CustomFeat", "  Input: %s, dtype: %d", name.c_str(), (int)tensor->dtype);
        if(tensor->shape.size() > 0) {
            ESP_LOGI("CustomFeat", "  Shape: [ ");
            for(auto s : tensor->shape) {
                ESP_LOGI("CustomFeat", " %d", (int)s);
            }
            ESP_LOGI("CustomFeat", "]");
        }
    }

    auto outputs = model->get_outputs();
    ESP_LOGI("CustomFeat", "Model has %d outputs:", (int)outputs.size());
    for (auto const& [name, tensor] : outputs) {
        ESP_LOGI("CustomFeat", "  Output: %s, dtype: %d", name.c_str(), (int)tensor->dtype);
    }

    // Preprocessing: (x - 127.5) / 127.5 -> range [-1.0, 1.0]
    // Result is then scaled by 2^-(-7) = 128 (via input exponent) to INT8 range
    ESP_LOGI("CustomFeat", "Creating ImagePreprocessor (Mean: 127.5, Std: 127.5, BGR: false)...");
    feat->set_preprocessor(new dl::image::FeatImagePreprocessor(model, {127.5, 127.5, 127.5}, {127.5, 127.5, 127.5}, false));
    ESP_LOGI("CustomFeat", "Creating Postprocessor...");
    feat->set_postprocessor(new dl::feat::FeatPostprocessor(model));
    
    // ESP_LOGI("CustomFeat", "Minimizing model...");
    // model->minimize(); // Removed to save memory and time as Recognition handles this
    ESP_LOGI("CustomFeat", "Model loaded and initialized successfully.");
    feat->m_ready = true;
    vTaskDelete(NULL);
}

class CustomHumanFaceRecognizer {
private:
    CustomFeat m_feat;
    dl::recognition::DataBase *m_db;
    std::string m_db_path;
    float m_thr;
    int m_top_k;

    const dl::detect::result_t* get_largest_face(const std::list<dl::detect::result_t> &detect_res) {
        if (detect_res.empty()) return nullptr;
        const dl::detect::result_t *max_face = &detect_res.front();
        float max_area = 0;
        for (auto it = detect_res.begin(); it != detect_res.end(); ++it) {
            float area = (float)(it->box[2] - it->box[0]) * (it->box[3] - it->box[1]);
            if (area > max_area) {
                max_area = area;
                max_face = &(*it);
            }
        }
        return max_face;
    }

    void normalize_feat(dl::TensorBase *feat) {
        if (!feat) return;
        int len = feat->get_size();
        float *data_ptr = nullptr;
        std::vector<float> temp_buf;

        if (feat->dtype == dl::DATA_TYPE_INT8) {
            int8_t *int8_ptr = (int8_t *)feat->get_element_ptr();
            
            float scale = pow(2, feat->exponent);
            temp_buf.resize(len);
            for (int i = 0; i < len; i++) {
                temp_buf[i] = (float)int8_ptr[i] * scale;
            }
            data_ptr = temp_buf.data();
            
            // Debug raw values
            // printf("Raw INT8[0-4]: %d %d %d %d %d | Scale: %f\n", int8_ptr[0], int8_ptr[1], int8_ptr[2], int8_ptr[3], int8_ptr[4], scale);
        } else if (feat->dtype == dl::DATA_TYPE_FLOAT) {
            data_ptr = (float *)feat->get_element_ptr();
        } else {
            ESP_LOGE("CustomFeat", "Unsupported dtype: %d", (int)feat->dtype);
            return;
        }

        // --- Safe L2 Normalization ---
        float sum = 0.0f;
        for (int i = 0; i < len; i++) sum += data_ptr[i] * data_ptr[i];
        float magnitude = sqrtf(sum);
        
        float norm = 0.0f;
        if (sum > 1e-10f && !isnan(sum)) {
            norm = 1.0f / magnitude;
        } else {
            ESP_LOGW("CustomFeat", "Invalid vector sum: %f", sum);
            norm = 0.0f; 
        }

        if (feat->dtype == dl::DATA_TYPE_FLOAT) {
            for (int i = 0; i < len; i++) data_ptr[i] *= norm;
        } else {
            // Update data_ptr after normalization in temp_buf
            for (int i = 0; i < len; i++) temp_buf[i] *= norm;
            data_ptr = temp_buf.data();
        }

        printf("Embed[0-4]: %.4f %.4f %.4f %.4f %.4f | Norm: %.4f\n", data_ptr[0], data_ptr[1], data_ptr[2], data_ptr[3], data_ptr[4], magnitude);
    }

public:
    CustomHumanFaceRecognizer(const std::string &db_path) : m_db_path(db_path), m_thr(0.30f), m_top_k(1) {
        int expected_len = m_feat.get_feat_len();
        m_db = new dl::recognition::DataBase(m_db_path, expected_len);
    }
    ~CustomHumanFaceRecognizer() { delete m_db; }

    std::vector<dl::recognition::result_t> recognize(const dl::image::img_t &img, const std::list<dl::detect::result_t> &detect_res) {
         auto target = get_largest_face(detect_res);
         if (!target) return {};
         auto feat = m_feat.run(img, target->keypoint);
         normalize_feat(feat);
         return m_db->query_feat(feat, m_thr, m_top_k);
    }

    esp_err_t enroll(const dl::image::img_t &img, const std::list<dl::detect::result_t> &detect_res) {
        auto target = get_largest_face(detect_res);
        if (!target) return ESP_FAIL;
        auto feat = m_feat.run(img, target->keypoint);
        normalize_feat(feat);
        return m_db->enroll_feat(feat);
    }

    esp_err_t clear_all_feats() { return m_db->clear_all_feats(); }
    int get_num_feats() { return m_db->get_num_feats(); }
};

static HumanFaceDetect *face_detect = nullptr;
static CustomHumanFaceRecognizer *face_recognizer = nullptr;
#else
static HumanFaceDetect *face_detect = nullptr;
static HumanFaceRecognizer *face_recognizer = nullptr;
#endif
static volatile bool is_enrolling = false;
static char enroll_name[32] = {0};
static volatile bool enable_recognize = true; // Default: Recognize Enabled

#include "app_web.h"

typedef struct {
    int id;
    char name[32];
    uint8_t *jpg_buf;
    size_t jpg_len;
} sync_task_arg_t;

static void sync_enroll_task(void *pvParameters) {
    sync_task_arg_t *arg = (sync_task_arg_t *)pvParameters;
    if (arg) {
        ESP_LOGI("app_face", "Async sync task running for ID %d (%s)...", arg->id, arg->name);
        app_web_sync_enroll_with_image(arg->id, arg->name, arg->jpg_buf, arg->jpg_len);
        if (arg->jpg_buf) {
            free(arg->jpg_buf);
        }
        free(arg);
    }
    vTaskDelete(NULL);
}

extern QueueHandle_t xQueueFrame;

// Shared results for async OSD
typedef struct {
    int box[4];
    char name[32];
    int recog_time; // Time in ms
    bool active;
} face_info_t;

static std::vector<face_info_t> shared_faces;
static SemaphoreHandle_t xFaceMutex = NULL;

// Note: Low-level font and drawing functions have been moved to app_graphics.cpp


// Fixed-size downsampled image for detection (160x120)
static uint16_t *small_img_buf = nullptr;

esp_err_t app_face_init(void) {
    face_detect = new HumanFaceDetect();
    if (!face_detect) return ESP_ERR_NO_MEM;
    char db_path[64];
    snprintf(db_path, sizeof(db_path), "%s/face_db", app_storage_get_path());

    int expected_len = 512; // Default for MFN
#if CONFIG_MODEL_TYPE_CUSTOM_MOBILENETV2
    expected_len = 128;
#elif CONFIG_MODEL_TYPE_MBF_S8_V1
    expected_len = 256;
#endif

    app_face_check_db_compatibility(db_path, expected_len);

#if CONFIG_MODEL_TYPE_CUSTOM_MOBILENETV2
    face_recognizer = new CustomHumanFaceRecognizer(db_path);
    ESP_LOGI(TAG, "Initialized with Custom MobileNetV2 Model");
#elif CONFIG_MODEL_TYPE_MBF_S8_V1
    face_recognizer = new HumanFaceRecognizer(db_path, HumanFaceFeat::MBF_S8_V1);
    ESP_LOGI(TAG, "Initialized with MBF_S8_V1 Model");
#else
    face_recognizer = new HumanFaceRecognizer(db_path, HumanFaceFeat::MFN_S8_V1);
    ESP_LOGI(TAG, "Initialized with MFN_S8_V1 Model");
#endif
    if (!face_recognizer) return ESP_ERR_NO_MEM;
    face_detect->set_score_thr(0.4f, 0); 
    face_detect->set_score_thr(0.4f, 1);
    
    xFaceMutex = xSemaphoreCreateMutex();
    
    // Allocate buffer for 160x120 RGB565 image
    small_img_buf = (uint16_t *)heap_caps_malloc(160 * 120 * 2, MALLOC_CAP_SPIRAM);
    
    return ESP_OK;
}

void app_face_draw_overlay(camera_fb_t *fb) {
    if (!xFaceMutex || !fb) return;
    if (xSemaphoreTake(xFaceMutex, 0) == pdTRUE) {
        uint16_t color = 0xE007; // Green BE
        for (const auto &face : shared_faces) {
            if (face.active) {
                draw_rect((uint16_t*)fb->buf, fb->width, fb->height, face.box[0], face.box[1], face.box[2], face.box[3], color);
                char label[64];
                if (face.recog_time > 0) {
                    snprintf(label, sizeof(label), "%s (%dms)", face.name, face.recog_time);
                } else {
                    snprintf(label, sizeof(label), "%s", face.name);
                }
                
                // Prevent drawing text off-screen when face is near the top edge
                int text_y = face.box[1] - 10;
                if (text_y < 0) text_y = face.box[3] + 5; // Draw below the bounding box if the top margin is too narrow
                if (text_y + 8 >= fb->height) text_y = face.box[1] + 5; // Draw inside the bounding box if the bottom margin is also narrow
                
                draw_string((uint16_t*)fb->buf, fb->width, fb->height, face.box[0], text_y, label, color);
            }
        }
        xSemaphoreGive(xFaceMutex);
    }
}

void app_face_draw_string(camera_fb_t *fb, int x, int y, const char *str, uint16_t color) {
    if (!fb || !fb->buf) return;
    draw_string((uint16_t*)fb->buf, fb->width, fb->height, x, y, str, color);
}

void app_face_ai_task(void *arg) {
    camera_fb_t *fb = NULL;
    uint32_t frame_count = 0;
    uint32_t no_face_count = 0;
    const uint32_t NO_FACE_LOG_INTERVAL = 100; // Log "no face" every ~100 frames (~5s at 20Hz)

    static bool s_last_light_state = false;
    static int s_no_face_frames = 0;

    ESP_LOGI(TAG, "🚀 AI Task started. Waiting for frames...");

    while (1) {
        if (xQueueReceive(xQueueFrame, &fb, portMAX_DELAY) == pdTRUE) {
            frame_count++;
            bool is_valid_id_recognized = false;

            int src_w = fb->width;
            int src_h = fb->height;
            int dst_w = src_w / 2;
            int dst_h = src_h / 2;

            // 1. Manually downsample to half size (Skip logic - Very Fast)
            uint16_t *src = (uint16_t *)fb->buf;
            uint16_t *dst = small_img_buf;
            for (int y = 0; y < dst_h; y++) {
                for (int x = 0; x < dst_w; x++) {
                    *dst++ = src[(y * 2) * src_w + (x * 2)];
                }
            }

            dl::image::img_t img_small;
            img_small.data = small_img_buf; img_small.width = dst_w; img_small.height = dst_h;
            img_small.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

            long long start_time = esp_timer_get_time();
            // 2. Perform detection on the SMALL image
            auto detect_results = face_detect->run(img_small);
            long long detect_time = (esp_timer_get_time() - start_time) / 1000;
            
            if (!detect_results.empty()) {
                no_face_count = 0; // Reset no-face counter

                // 3. Scale small detection boxes and keypoints back to 320x240 for recognition/enrollment
                for (auto &dr : detect_results) {
                    for (int i = 0; i < 4; i++) dr.box[i] *= 2;
                    for (size_t i = 0; i < dr.keypoint.size(); i++) dr.keypoint[i] *= 2;
                }

                // --- Update bounding box immediately for smooth LCD rendering ---
                if (xSemaphoreTake(xFaceMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    std::vector<face_info_t> next_faces;
                    for (const auto &dr : detect_results) {
                        face_info_t info;
                        for(int i=0; i<4; i++) info.box[i] = dr.box[i];
                        info.active = true;
                        info.recog_time = 0;
                        
                        // Attempt to retain the old name if the face remains in the same general position (Simple tracking)
                        bool found_old = false;
                        for (const auto &old : shared_faces) {
                            int old_cx = (old.box[0] + old.box[2]) / 2;
                            int old_cy = (old.box[1] + old.box[3]) / 2;
                            int dr_cx = (dr.box[0] + dr.box[2]) / 2;
                            int dr_cy = (dr.box[1] + dr.box[3]) / 2;
                            // If the center shifts less than 60px, consider it the same person
                            if (abs(old_cx - dr_cx) < 60 && abs(old_cy - dr_cy) < 60) {
                                strcpy(info.name, old.name);
                                info.recog_time = old.recog_time;
                                found_old = true;
                                break;
                            }
                        }
                        if (!found_old) strcpy(info.name, enable_recognize ? "Detecting..." : "Detect Only");
                        next_faces.push_back(info);
                    }
                    shared_faces = next_faces;
                    xSemaphoreGive(xFaceMutex);
                }

                if (is_enrolling) {
                    ESP_LOGI(TAG, "📸 ENROLLING '%s' - Face found! Processing...", enroll_name);

                    dl::image::img_t img_full;
                    img_full.data = fb->buf; img_full.width = fb->width; img_full.height = fb->height;
                    img_full.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;
                    
                    if (face_recognizer->enroll(img_full, detect_results) == ESP_OK) {
                        int id = face_recognizer->get_num_feats();
                        app_storage_set_name(id, enroll_name);
                        ESP_LOGI(TAG, "✅ ENROLL SUCCESS: '%s' -> ID:%d (Total: %d faces in DB)", enroll_name, id, id);
                        app_web_send_log_formatted("Enrollment success: '%s' (ID: %d)", enroll_name, id);
                        is_enrolling = false; // Only stop once successful
                        
#if CONFIG_ENABLE_WEB_SERVER
                        uint8_t *jpg_buf = NULL;
                        size_t jpg_len = 0;
                        bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
                        if (jpeg_converted) {
                            ESP_LOGI(TAG, "JPEG conversion success: %d bytes. Spawning async sync task...", jpg_len);
                            sync_task_arg_t *arg = (sync_task_arg_t *)malloc(sizeof(sync_task_arg_t));
                            if (arg) {
                                arg->id = id;
                                strncpy(arg->name, enroll_name, sizeof(arg->name) - 1);
                                arg->name[sizeof(arg->name) - 1] = '\0';
                                arg->jpg_buf = (uint8_t *)malloc(jpg_len);
                                if (arg->jpg_buf) {
                                    memcpy(arg->jpg_buf, jpg_buf, jpg_len);
                                    arg->jpg_len = jpg_len;
                                    xTaskCreate(sync_enroll_task, "sync_enroll", 4096, arg, 5, NULL);
                                } else {
                                    free(arg);
                                }
                            }
                            free(jpg_buf);
                        } else {
                            ESP_LOGE(TAG, "JPEG conversion failed. Doing fallback sync.");
                            app_web_sync_enroll(id, enroll_name);
                        }
#endif
                    } else {
                        ESP_LOGW(TAG, "⚠️ ENROLL FAILED for '%s' - Feature extraction error. Keep face steady!", enroll_name);
                        app_web_send_log_formatted("Enrollment failed for '%s' (Feature extraction error)", enroll_name);
                    }
                } else if (enable_recognize) {
                    dl::image::img_t img_full;
                    img_full.data = fb->buf; img_full.width = fb->width; img_full.height = fb->height;
                    img_full.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;
                    
                    long long rec_start = esp_timer_get_time();
                    auto recog_results = face_recognizer->recognize(img_full, detect_results);
                    long long rec_time = (esp_timer_get_time() - rec_start) / 1000;
                    
                    if (xSemaphoreTake(xFaceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        shared_faces.clear();
                        auto det = detect_results.begin();
                        
                        if (recog_results.empty()) {
                            // No match found in database even with low threshold
                            face_info_t info;
                            for(int i=0; i<4; i++) info.box[i] = det->box[i];
                            strcpy(info.name, "Stranger");
                            info.recog_time = (int)rec_time;
                            info.active = true;
                            shared_faces.push_back(info);
                            ESP_LOGW(TAG, "👤 STRANGER: No match in database | Detect: %lld ms | Recog: %lld ms", detect_time, rec_time);
                            app_web_send_log_formatted("Stranger detected (No match in database)");
                        } else {
                            for (auto &res : recog_results) {
                                face_info_t info;
                                for(int i=0; i<4; i++) info.box[i] = det->box[i];
                                
                                if (res.similarity > FACE_RECOGNITION_THRESHOLD) {
                                    if (app_storage_get_name(res.id, info.name, sizeof(info.name)) != ESP_OK) {
                                        snprintf(info.name, sizeof(info.name), "ID:%d", res.id);
                                    }
                                    ESP_LOGI(TAG, "✅ Recognized: %s | ID:%d | Sim: %.2f | Detect: %lld ms | Recog: %lld ms", 
                                             info.name, res.id, res.similarity, detect_time, rec_time);
                                    app_web_send_log_formatted("Recognized: %s (ID: %d, Sim: %.2f)", info.name, res.id, res.similarity);
                                    is_valid_id_recognized = true;
                                } else if (res.similarity > FACE_STRANGER_THRESHOLD) {
                                    // Potential match but below strict threshold
                                    char possible_name[32];
                                    if (app_storage_get_name(res.id, possible_name, sizeof(possible_name)) != ESP_OK) {
                                        snprintf(possible_name, sizeof(possible_name), "ID:%d", res.id);
                                    }
                                    strcpy(info.name, "Stranger");
                                    ESP_LOGW(TAG, "⚠️ LOW CONFIDENCE: Possible %s? | Sim: %.2f | Detect: %lld ms | Recog: %lld ms | 💡 Register if this is %s", 
                                             possible_name, res.similarity, detect_time, rec_time, possible_name);
                                    app_web_send_log_formatted("Low confidence: Possible %s (Sim: %.2f)", possible_name, res.similarity);
                                } else {
                                    // Similarity too low, definitely a stranger
                                    strcpy(info.name, "Stranger");
                                    ESP_LOGW(TAG, "👤 STRANGER: Unknown face | Sim: %.2f | Detect: %lld ms | Recog: %lld ms", 
                                             res.similarity, detect_time, rec_time);
                                    app_web_send_log_formatted("Stranger detected (Sim: %.2f)", res.similarity);
                                }
                                
                                info.recog_time = (int)rec_time;
                                info.active = true;
                                shared_faces.push_back(info);
                                if (det != detect_results.end()) det++;
                            }
                        }
                        xSemaphoreGive(xFaceMutex);
                    }
                }
            } else {
                no_face_count++;
                if (xSemaphoreTake(xFaceMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    shared_faces.clear();
                    xSemaphoreGive(xFaceMutex);
                }
                // Periodic heartbeat when no face detected (avoid log flood)
                if (no_face_count % NO_FACE_LOG_INTERVAL == 0) {
                    ESP_LOGD(TAG, "... No face detected for %lu frames (Frame #%lu) | Detect: %lld ms",
                             (unsigned long)no_face_count, (unsigned long)frame_count, detect_time);
                }
                // Log if enrolling but no face found
                if (is_enrolling && (no_face_count % 20 == 1)) {
                    ESP_LOGW(TAG, "📸 ENROLL WAITING: No face detected! Point camera at '%s'", enroll_name);
                }
            }

            // ESP-NOW Light Control State Machine (NexusIR LED control protocol)
            if (is_valid_id_recognized) {
                s_no_face_frames = 0;
                if (!s_last_light_state) {
                    app_espnow_send_light_on();
                    s_last_light_state = true;
                }
            } else {
                if (s_last_light_state) {
                    s_no_face_frames++;
                    if (s_no_face_frames >= 60) { // ~3 seconds debounce hysteresis at ~20Hz
                        app_espnow_send_light_off();
                        s_last_light_state = false;
                        s_no_face_frames = 0;
                    }
                }
            }

            app_camera_release(fb); 
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void app_face_enroll(const char *name) {
    is_enrolling = true;
    strncpy(enroll_name, name, sizeof(enroll_name)-1);
}

void app_face_toggle_recognize(void) {
    enable_recognize = !enable_recognize;
    ESP_LOGI(TAG, "Recognition is now %s", enable_recognize ? "ON" : "OFF");
}

void app_face_enroll_auto(void) {
    if (!face_recognizer) return;
    int next_id = face_recognizer->get_num_feats() + 1;
    char name[32];
    snprintf(name, sizeof(name), "User_%d", next_id);
    app_face_enroll(name);
}

void app_face_delete_all(void) {
    if (face_recognizer) face_recognizer->clear_all_feats();
}

int app_face_get_count(void) {
    if (face_recognizer) return face_recognizer->get_num_feats();
    return 0;
}
