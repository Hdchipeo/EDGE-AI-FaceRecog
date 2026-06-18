# Kế hoạch triển khai Project: Face Recognition on Edge AI (ESP32-S3)

## 1. Tổng quan hệ thống
* **Hardware:** ESP32-S3 N16R8, Camera OV3660.
* **Framework:** ESP-IDF (v4.4 hoặc v5.0+), ESP-DL.
* **Model:** MobileNetV2 (Backbone) + Face Recognition Head.
* **Optimization:** Quantization INT8 bằng ESP-PPQ.

---

## 2. Lộ trình triển khai

### Giai đoạn 1: Chuẩn bị & Thu thập dữ liệu
* [ ] **Thu thập Dataset:** * Chụp 50 ảnh/thành viên với nhiều góc độ, điều kiện ánh sáng và biểu cảm khác nhau.
    * Sử dụng Data Augmentation (xoay, lật, thay đổi độ sáng) để nhân bản tập dữ liệu lên khoảng 200-300 ảnh.
* [ ] **Setup Môi trường:**
    * Cài đặt ESP-IDF và clone repository `esp-dl` từ Espressif.
    * Cài đặt môi trường Python (TensorFlow/PyTorch, ONNX, PPQ).

### Giai đoạn 2: Huấn luyện & Chuyển đổi mô hình
* [ ] **Training (Transfer Learning):** * Sử dụng MobileNetV2 pre-trained trên tập FaceNet.
    * Huấn luyện model trích xuất vector đặc trưng (Embedding) trên tập LFW.
    * Kiểm tra độ chính xác bằng khoảng cách Euclidean hoặc Cosine giữa các cặp ảnh.
    * Huấn luyện lớp phân loại (hoặc trích xuất embeddings) cho 2 thành viên nhóm.
* [ ] **Xuất mô hình:** Xuất file sang định dạng `.onnx`.
* [ ] **Quantization (ESP-PPQ):**
    * Sử dụng tập dữ liệu calibration để chuyển đổi model từ Float32 sang INT8.
    * Kiểm tra độ chính xác của model sau khi định lượng trên PC.

### Giai đoạn 3: Lập trình trên ESP32-S3
* [ ] **Cấu hình Camera:** Viết driver cho OV3660, tối ưu hóa bộ đệm (frame buffer) trên PSRAM.
* [ ] **Tích hợp ESP-DL:**
    * Import mô hình đã định lượng vào project C++.
    * Triển khai pipeline: `Camera -> Image Preprocessing -> Face Detection -> Face Recognition`.
* [ ] **Tối ưu hóa bộ nhớ:** Cấu hình phân bổ vùng nhớ trên 8MB PSRAM để tránh lỗi tràn bộ nhớ (Heap overflow).

### Giai đoạn 4: Kiểm thử & Hoàn thiện
* [ ] **Đánh giá hiệu năng:** Đo lường FPS (Frames Per Second) và tỷ lệ nhận diện chính xác (Accuracy).
* [ ] **Xử lý ngoại lệ:** Tối ưu hóa việc nhận diện trong môi trường thiếu sáng hoặc khi đeo kính/khẩu trang.
* [ ] **Đóng gói:** Viết báo cáo, hoàn thiện file README và quay video demo.

---

## 3. Kiến trúc Pipeline xử lý



1.  **Image Capture:** Chụp ảnh RGB565/JPEG từ OV3660.
2.  **Detection (MNN/SCoP):** Tìm bounding box của khuôn mặt.
3.  **Alignment:** Cân chỉnh khuôn mặt về kích thước chuẩn (ví dụ: $96 \times 96$ hoặc $112 \times 112$).
4.  **Feature Extraction:** MobileNetV2 (INT8) tạo ra vector embedding đặc trưng.
5.  **Classification:** So sánh khoảng cách với vector mẫu để xác định danh tính.

---

## 4. Danh mục vật tư & Công cụ
| Thành phần | Chi tiết | Ghi chú |
| :--- | :--- | :--- |
| **SoC** | ESP32-S3 | Hỗ trợ tập lệnh AI |
| **Storage** | 16MB Flash / 8MB PSRAM | Lưu trữ model và buffer |
| **Sensor** | OV3660 | Camera 3MP |
| **Phần mềm** | ESP-IDF, ESP-DL | Framework chính |
| **Công cụ AI** | Python, ONNX, PPQ | Huấn luyện & Nén |

---

## 5. Các rủi ro và Giải pháp
* **Rủi ro 1:** Tốc độ suy luận chậm (< 2 FPS).
    * *Giải pháp:* Giảm hệ số `width_mult` của MobileNetV2 hoặc giảm độ phân giải đầu vào.
* **Rủi ro 2:** Nhận diện nhầm giữa 2 thành viên.
    * *Giải pháp:* Tăng ngưỡng (threshold) khoảng cách và bổ sung thêm ảnh chụp nghiêng.
* **Rủi ro 3:** Thiếu bộ nhớ RAM.
    * *Giải pháp:* Ép kiểu dữ liệu sang `int8` và giải phóng buffer ngay sau khi sử dụng.

## 6. Tài liệu chính thức từ Espressif

  * **[ESP-DL Programming Guide](https://docs.espressif.com/projects/esp-dl/en/latest/index.html):** Tài liệu hướng dẫn chi tiết về thư viện học sâu của Espressif, các API cho model inference và tối ưu hóa phần cứng.
  * **[ESP-PPQ GitHub](https://github.com/espressif/esp-ppq):** Công cụ định lượng (Quantization) để chuyển đổi model ONNX/PyTorch sang định dạng INT8.
  * **[ESP-WHO Framework](https://github.com/espressif/esp-who):** Các ví dụ mẫu về nhận diện khuôn mặt và xử lý hình ảnh trên dòng chip ESP32.

## 7. Dữ liệu & Kiến trúc

  * **LFW Dataset:** [Labeled Faces in the Wild Home Page](https://www.google.com/search?q=http://vis-www.cs.umass.edu/lfw/).
  * **MobileNetV2 Paper:** [Inverted Residuals and Linear Bottlenecks](https://arxiv.org/abs/1801.04381).


