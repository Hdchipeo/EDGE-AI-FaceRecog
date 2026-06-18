import os
import glob
import cv2
import numpy as np
import torch
from esp_ppq import *
from esp_ppq.api import *
from esp_ppq.core import TargetPlatform

def load_calibration_dataset(dataset_dir, batch_size=1, input_shape=(112, 112, 3)):
    image_paths = glob.glob(os.path.join(dataset_dir, "**", "*.jpg"), recursive=True)
    if len(image_paths) == 0:
        raise ValueError(f"No images found in {dataset_dir}")
    
    print(f"[INFO] Found {len(image_paths)} calibration images.")
    
    batches = []
    for path in image_paths[:100]: # limit to 100 images for speed
        img = cv2.imread(path)
        if img is None: continue
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = cv2.resize(img, (input_shape[1], input_shape[0]))
        
        # MobileNetV2 expects input images normalized to range [-1.0, 1.0]
        # (Matching the (x/127.5) - 1.0 operation in the ESP firmware)
        img = (img.astype(np.float32) / 127.5) - 1.0
        
        # The returned numpy array must have shape [batch, W, H, C] if the model is NHWC (TensorFlow default),
        # or [batch, C, H, W] if NCHW. Since we exported from Keras, ONNX defaults to [batch, H, W, C].
        img = np.expand_dims(img, axis=0) 
        
        # Convert to torch tensor since PPQ works with PyTorch tensors under the hood
        batches.append(torch.from_numpy(img))
        
    return batches

def main():
    onnx_path = "models/mobilenetv2_128d_v3.onnx" 
    calib_dir = "dataset/cropped"
    export_path = "models/mobilenetv2_128d_v3.espdl"
    
    # Prioritize the simplified version of the ONNX graph
    sim_onnx_path = "models/mobilenetv2_128d_v3_sim.onnx"
    if os.path.exists(sim_onnx_path):
        onnx_path = sim_onnx_path
        print(f"[INFO] Found simplified model, using {onnx_path}")
    else:
        print(f"[WARNING] Simplified model file ({sim_onnx_path}) not found. Fallback to {onnx_path}.")

    print("[INFO] Loading calibration dataset...")
    calib_dataloader = load_calibration_dataset(calib_dir)
    
    print("[INFO] Initializing ESP-DL Quantization Settings...")
    quant_setting = QuantizationSettingFactory.espdl_setting()
    quant_setting.graph_equalization = False # Disable graph equalization per configuration protocol
    
    print(f"[INFO] Running PPQ Quantization on {onnx_path}...")
    quantized_graph = quantize_onnx_model(
        onnx_import_file=onnx_path,
        calib_dataloader=calib_dataloader,
        calib_steps=len(calib_dataloader),
        input_shape=[1, 112, 112, 3],
        setting=quant_setting,
        platform=TargetPlatform.ESPDL_S3_INT8,
        device='cpu'
    )
    
    # Assign target platform to all ops for proper ESP32-S3 hardware acceleration
    for op in quantized_graph.operations.values():
        op.platform = TargetPlatform.ESPDL_S3_INT8
        # Ensure Transpose nodes (usually at input/output) are INT8 quantized
        if op.type == 'Transpose':
            for var in op.outputs:
                var.dtype = DataType.INT8

    # Force input type to INT8 to enable ImagePreprocessor hardware acceleration
    for input_var in quantized_graph.inputs.values():
        input_var.dtype = DataType.INT8
        print(f"[FIX] Forced input '{input_var.name}' to INT8")

    # Configure Exponent for Input so ImagePreprocessor knows scale factor.
    # Range [-1.0, 1.0] corresponds to exponent -7 (scale 1/128)
    from esp_ppq.parser.espdl.espdl_typedef import ExporterPatternInfo
    export_info = ExporterPatternInfo()
    if 'input_image' in quantized_graph.inputs:
        export_info.add_var_exponents('input_image', [-7])
        print("[FIX] Injected exponent -7 for 'input_image'")

    print("[INFO] Exporting quantized model to .espdl format...")
    export_ppq_graph(
        graph=quantized_graph, 
        platform=TargetPlatform.ESPDL_S3_INT8,
        graph_save_to=export_path,
        config_save_to=export_path.replace(".espdl", ".json"),
        export_config=export_info
    )
    
    print(f"[SUCCESS] Quantized model saved to {export_path}")
    print("[INFO] Please copy the exported files (*.espdl, *.json, *.info) to 'Source/FaceRecognitionS3/main/models/' and rebuild the project.")

if __name__ == "__main__":
    main()
