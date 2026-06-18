import tensorflow as tf
import tf2onnx
import os

# Configuration
MODEL_PATH = "models/mobilenetv2_128d_v3.onnx"
INPUT_SHAPE = (112, 112, 3) # Input shape normalization (W, H, C)
WEIGHTS_PATH = None # Path to custom pre-trained weights (.h5) on MS-Celeb-1M, if available

def build_model():
    """
    Builds the MobileNetV2 architecture with 128D Embedding for Transfer Learning.
    """
    # 1. Base Model (Backbone)
    # If using local weights, keep weights=None and load later with model.load_weights().
    base_model = tf.keras.applications.MobileNetV2(
        input_shape=INPUT_SHAPE,
        include_top=False,
        weights='imagenet' if WEIGHTS_PATH is None else None, 
        alpha=1.0 # Width multiplier (can reduce to 0.75, 0.5 for smaller models)
    )
    
    # Freeze backbone weights if used strictly as a feature extractor
    base_model.trainable = False

    # 2. Input Pipeline
    inputs = tf.keras.Input(shape=INPUT_SHAPE, name='input_image')
    
    # NOTE: In ESP-DL, the FeatImagePreprocessor performs normalization in hardware!
    # Therefore, do NOT add a Rescaling layer to the ONNX graph; otherwise, ESP-PPQ
    # will preserve the input as FLOAT32 instead of INT8, crashing on ESP32-S3.
    # Pass input directly into backbone:
    x = inputs
    
    # Pass through backbone
    x = base_model(x)
    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    
    # 3. Head: 128-dimensional embedding output layer.
    # NOTE: Layer name must be 'output_embedding' for the ESP-DL firmware to identify it.
    # UnitNormalization is skipped here to prevent unsupported Rsqrt ops on ESP32-S3.
    outputs = tf.keras.layers.Dense(128, activation=None, name='output_embedding')(x)
    
    # Wrap model
    model = tf.keras.Model(inputs, outputs, name="MobileNetV2_Face_128D")
    return model

def export_to_onnx(model, output_path):
    print("\n[INFO] Exporting model to ONNX format...")
    spec = (tf.TensorSpec((None, *INPUT_SHAPE), tf.float32, name="input_image"),)
    
    # Opset 13 is highly compatible with modern quantization tools like ESP-PPQ
    model_proto, _ = tf2onnx.convert.from_keras(
        model, 
        input_signature=spec, 
        opset=13, 
        output_path=output_path
    )
    print(f"[SUCCESS] ONNX model successfully exported to: {output_path}")

    # --- OPTIMIZATION STEP ---
    try:
        import onnxsim
        print("[INFO] Optimizing graph using ONNX Simplifier...")
        import onnx
        model_onnx = onnx.load(output_path)
        model_simp, check = onnxsim.simplify(model_onnx)
        if check:
            onnx.save(model_simp, output_path)
            print("[SUCCESS] ONNX graph simplified and redundant nodes removed.")
        else:
            print("[WARNING] ONNX Simplifier validation failed. Retaining original model.")
    except ImportError:
        print("[TIP] Install 'onnxsim' (pip install onnxsim) for enhanced graph optimization.")

def main():
    # 1. Initialize model
    model = build_model()
    model.summary()
    
    # 2. Load custom weights (if pre-trained on MS-Celeb-1M)
    if WEIGHTS_PATH and os.path.exists(WEIGHTS_PATH):
        print(f"[INFO] Loading pre-trained weights from: {WEIGHTS_PATH}")
        # by_name=True and skip_mismatch=True allow loading weights from base networks
        # into new models even if the final layers mismatch.
        model.load_weights(WEIGHTS_PATH, by_name=True, skip_mismatch=True)
    else:
        print("[WARNING] Custom weights path not found. Using default ImageNet weights.")
        print("-> Tip: Train (Transfer Learning) the model on MS-Celeb-1M or your custom dataset before exporting.")

    # 3. Export to ONNX
    export_to_onnx(model, MODEL_PATH)

if __name__ == "__main__":
    # Disable TensorFlow verbose logs
    os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'
    main()
