# Hướng Dẫn Huấn Luyện và Tối Ưu Mô Hình (TrainModel)

Thư mục này chứa toàn bộ mã nguồn và quy trình xử lý dữ liệu ảnh, thiết lập mô hình, tối ưu hóa đồ thị và lượng tử hóa mô hình nhận diện khuôn mặt MobileNetV2 (128D Embedding) để deploy lên vi điều khiển ESP32-S3 sử dụng thư viện **ESP-DL** và bộ công cụ **ESP-PPQ**.

---

## Cấu Trúc Thư Mục Sau Tối Ưu

```
TrainModel/
├── dataset/                  # Dữ liệu ảnh phân loại theo từng người
│   ├── original/             # Ảnh gốc thu thập được
│   ├── cropped/              # Ảnh khuôn mặt đã cắt (112x112)
│   └── augmented/            # Ảnh đã qua tăng cường dữ liệu
├── docs/                     # Tài liệu và sơ đồ cấu trúc
│   └── directory_structure.png
├── models/                   # Thư mục lưu trữ các phiên bản mô hình ONNX và ESPDL
│   ├── mobilenetv2_128d_v3.onnx      # Mô hình ONNX thô xuất từ Keras
│   ├── mobilenetv2_128d_v3_sim.onnx  # Mô hình ONNX đã tối ưu đồ thị & BN fusion
│   ├── mobilenetv2_128d_v3.espdl     # Mô hình lượng tử hóa INT8 cho ESP32-S3
│   ├── mobilenetv2_128d_v3.json      # File cấu hình lượng tử hóa
│   └── mobilenetv2_128d_v3.info      # File thông tin chi tiết lượng tử hóa
├── requirements.txt          # Các thư viện Python cần thiết
├── venv/                     # Môi trường ảo Python (Virtual Environment)
├── crop_faces.py             # Script cắt khuôn mặt tự động
├── augment_dataset.py        # Script tăng cường dữ liệu ảnh
├── export_model.py           # Script build mô hình Keras và export sang ONNX
├── fix_onnx.py               # Script sửa lỗi tương thích Pad/Conv cho ESP-DL
├── fuse_bn.py                # Script gộp Batch Normalization thủ công
└── quantize_model.py         # Script lượng tử hóa mô hình sang dạng INT8
```

---

## Hướng Dẫn Cài Đặt Môi Trường

1. **Khởi tạo và kích hoạt môi trường ảo Python**:
   ```bash
   python3 -m venv venv
   source venv/bin/activate  # Trên macOS/Linux
   # hoặc venv\Scripts\activate trên Windows
   ```

2. **Cài đặt các thư viện cơ bản**:
   ```bash
   pip install -r requirements.txt
   ```

3. **Cài đặt thư viện lượng tử hóa ESP-PPQ**:
   Để chạy được script lượng tử hóa `quantize_model.py`, bạn cần cài đặt thư viện `esp-ppq` của Espressif. Thư viện này chạy trên nền tảng PyTorch.
   ```bash
   pip install torch torchvision
   # Cài đặt ESP-PPQ từ source hoặc theo hướng dẫn chính thức của Espressif
   # Link tham khảo: https://github.com/espressif/esp-ppq
   ```

---

## Quy Trình Các Bước Thực Hiện (Pipeline)

Bạn cần thực hiện các script theo đúng thứ tự dưới đây để chuẩn bị dữ liệu, xuất và tối ưu hóa mô hình.

### Bước 1: Thu thập ảnh gốc
- Đặt các ảnh gốc thu thập được của từng người vào thư mục `dataset/original/<tên_người>/` (Ví dụ: `dataset/original/person1/`, `dataset/original/person2/`).

### Bước 2: Cắt khuôn mặt (Face Cropping)
- Chạy script `crop_faces.py` để tự động quét toàn bộ thư mục ảnh gốc, sử dụng Haar Cascade để phát hiện khuôn mặt, crop lấy vùng mặt lớn nhất, đệm thêm 10% biên và chuẩn hóa về kích thước 112x112.
- Lệnh chạy:
  ```bash
  python crop_faces.py
  ```
- Kết quả được lưu tại thư mục: `dataset/cropped/<tên_người>/`.

### Bước 3: Tăng cường dữ liệu (Data Augmentation)
- Chạy script `augment_dataset.py` để tạo thêm các ảnh biến thể từ ảnh đã cắt (lật ngang, xoay nhẹ, đổi độ sáng tối, tạo nhiễu camera ESP32) giúp mô hình học tốt hơn và tránh overfit.
- Lệnh chạy:
  ```bash
  python augment_dataset.py
  ```
- Kết quả lưu tại thư mục: `dataset/augmented/<tên_người>/`.

### Bước 4: Xây dựng và Xuất mô hình sang ONNX
- Chạy script `export_model.py` để khởi tạo mô hình Transfer Learning sử dụng backbone MobileNetV2 (weights ImageNet) với output là một Dense Layer 128 chiều (đặt tên layer là `output_embedding`).
- **Lưu ý đặc biệt**: Không thêm Rescaling layer trực tiếp vào đồ thị ONNX vì bộ tiền xử lý phần cứng `FeatImagePreprocessor` trên ESP32-S3 sẽ tự thực hiện scale ảnh. Nếu giữ Rescaling layer, ESP-PPQ sẽ bắt buộc giữ kiểu dữ liệu Input là FLOAT32 thay vì INT8, gây lỗi crash firmware.
- Lệnh chạy:
  ```bash
  python export_model.py
  ```
- Kết quả lưu tại: `models/mobilenetv2_128d_v3.onnx`.

### Bước 5: Gộp Batch Normalization (BN Fusion)
- Chạy script `fuse_bn.py` để tối ưu đồ thị ONNX thủ công. Script này sẽ tìm các node Conv ghép sau bởi Mul và Add (dạng BN phân rã khi export từ Keras) rồi gộp trực tiếp trọng số và bias vào node Conv trước đó, giúp giảm số lượng node và tăng tốc độ xử lý trên chip ESP32-S3.
- Lệnh chạy:
  ```bash
  python fuse_bn.py
  ```
- Kết quả lưu tại: `models/mobilenetv2_128d_v3_sim.onnx`.

### Bước 6: Sửa các lỗi tương thích đồ thị ONNX
- Chạy script `fix_onnx.py` để sửa các thuộc tính Pad của Conv và đổi chế độ auto_pad thành `NOTSET` (theo yêu cầu bắt buộc của thư viện ESP-DL).
- Lệnh chạy:
  ```bash
  python fix_onnx.py
  ```
- Kết quả sẽ ghi đè trực tiếp lên file mô hình để hoàn tất khâu chuẩn bị ONNX.

### Bước 7: Lượng tử hóa mô hình sang INT8 (Quantization)
- Chạy script `quantize_model.py` để thực hiện lượng tử hóa đồ thị ONNX sang định dạng `.espdl` INT8. Script này sử dụng tập dữ liệu `dataset/cropped/` làm tập dữ liệu calibration để tính toán dải phân bố trọng số và cấu hình Exponent -7 (scale 1/128) phù hợp với ngõ vào ảnh `[-1.0, 1.0]`.
- Lệnh chạy:
  ```bash
  python quantize_model.py
  ```
- Kết quả lưu tại thư mục `models/` gồm 3 file chính:
  - `mobilenetv2_128d_v3.espdl` (Mô hình nhị phân đã lượng tử hóa)
  - `mobilenetv2_128d_v3.json` (File cấu hình)
  - `mobilenetv2_128d_v3.info` (File thông tin chi tiết)

---

## Hướng Dẫn Tích Hợp Vào Firmware ESP32-S3

Sau khi hoàn thành bước 7:
1. Sao chép 3 file output lượng tử hóa từ `TrainModel/models/` sang thư mục mô hình của project firmware tại:
   `Source/FaceRecognitionS3/main/models/`
2. Tiến hành build lại project firmware bằng ESP-IDF:
   ```bash
   idf.py build
   idf.py -p <PORT> flash monitor
   ```
