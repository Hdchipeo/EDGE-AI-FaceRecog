import cv2
import os
import albumentations as A
import numpy as np
from pathlib import Path

# Path configuration
INPUT_DIR = "dataset/cropped"
OUTPUT_DIR = "dataset/augmented"
NUM_AUGMENTATIONS = 10 # Number of augmented images generated per original image

def main():
    Path(OUTPUT_DIR).mkdir(parents=True, exist_ok=True)
    
    # Define augmentation pipeline (based on OpenCV attributes)
    transform = A.Compose([
        A.HorizontalFlip(p=0.5), # 50% chance of horizontal flip
        A.Rotate(limit=15, p=0.8, border_mode=cv2.BORDER_CONSTANT), # Rotate up to 15 degrees
        A.RandomBrightnessContrast(brightness_limit=0.2, contrast_limit=0.2, p=0.8), # Random brightness/contrast adjustments
        A.GaussNoise(var_limit=(10.0, 50.0), p=0.5) # Gaussian noise to simulate ESP32 camera sensor noise
    ])

    print(f"[INFO] Starting image augmentation from '{INPUT_DIR}'...")
    total_images_processed = 0
    total_augmented = 0

    # Traverse subdirectories (e.g., person1, person2)
    for root, dirs, files in os.walk(INPUT_DIR):
        for file in files:
            if file.lower().endswith(('.png', '.jpg', '.jpeg')):
                filepath = os.path.join(root, file)
                
                # Create corresponding output subdirectory
                rel_path = os.path.relpath(root, INPUT_DIR)
                out_dir = os.path.join(OUTPUT_DIR, rel_path)
                Path(out_dir).mkdir(parents=True, exist_ok=True)
                
                # Read original image
                img = cv2.imread(filepath)
                if img is None:
                    print(f"[ERROR] Failed to read image: {filepath}")
                    continue
                
                # Resize image to MobileNetV2 dimensions (112x112)
                # Note: Input images are expected to be cropped face ROIs.
                img_resized = cv2.resize(img, (112, 112))
                
                # Save the resized original image
                base_name = os.path.splitext(file)[0]
                cv2.imwrite(os.path.join(out_dir, f"{base_name}_orig.jpg"), img_resized)
                total_images_processed += 1
                
                # Generate augmented images
                for i in range(NUM_AUGMENTATIONS):
                    augmented = transform(image=img_resized)
                    aug_img = augmented['image']
                    cv2.imwrite(os.path.join(out_dir, f"{base_name}_aug_{i}.jpg"), aug_img)
                    total_augmented += 1
                    
    print("="*40)
    print("DATASET AUGMENTATION COMPLETED!")
    print(f"- Original images processed: {total_images_processed}")
    print(f"- Augmented images generated: {total_augmented}")
    print(f"- Output directory:          {OUTPUT_DIR}")
    print("="*40)

if __name__ == "__main__":
    if not os.path.exists(INPUT_DIR) or len(os.listdir(INPUT_DIR)) == 0:
        print(f"[WARNING] Please place cropped face images into '{INPUT_DIR}/<person_name>/' first.")
    else:
        main()
