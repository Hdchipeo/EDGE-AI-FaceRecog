import cv2
import os
from pathlib import Path

# Configuration paths
INPUT_DIR = "dataset/original"
OUTPUT_DIR = "dataset/cropped"

def main():
    Path(OUTPUT_DIR).mkdir(parents=True, exist_ok=True)
    
    # Use OpenCV's pre-trained Haar Cascade classifier for face detection
    face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
    
    total_images = 0
    total_faces_found = 0

    print(f"[INFO] Scanning and cropping faces from '{INPUT_DIR}'...")
    for root, dirs, files in os.walk(INPUT_DIR):
        for file in files:
            if file.lower().endswith(('.png', '.jpg', '.jpeg')):
                filepath = os.path.join(root, file)
                rel_path = os.path.relpath(root, INPUT_DIR)
                out_dir = os.path.join(OUTPUT_DIR, rel_path)
                Path(out_dir).mkdir(parents=True, exist_ok=True)
                
                img = cv2.imread(filepath)
                if img is None:
                    print(f"[ERROR] Failed to read image: {filepath}")
                    continue
                total_images += 1
                
                gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
                # Detect faces
                faces = face_cascade.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=5, minSize=(100, 100))
                
                if len(faces) > 0:
                    # Select only the largest detected face in the image
                    faces = sorted(faces, key=lambda x: x[2]*x[3], reverse=True)
                    x, y, w, h = faces[0]
                    
                    # Add 10% padding around the bounding box to capture chin and hair
                    pad = int(w * 0.1)
                    x1 = max(0, x - pad)
                    y1 = max(0, y - pad)
                    x2 = min(img.shape[1], x + w + pad)
                    y2 = min(img.shape[0], y + h + pad)
                    
                    face_roi = img[y1:y2, x1:x2]
                    face_roi_resized = cv2.resize(face_roi, (112, 112)) # Normalize output face dimensions
                    
                    out_path = os.path.join(out_dir, file)
                    cv2.imwrite(out_path, face_roi_resized)
                    total_faces_found += 1
                    print(f"[SUCCESS] Cropped: {out_path}")
                else:
                    print(f"[WARNING] Face not detected in: {filepath}")

    print("="*40)
    print("FACE CROPPING COMPLETED!")
    print(f"- Total images scanned: {total_images}")
    print(f"- Total faces cropped:  {total_faces_found}")
    print(f"- Output directory:     {OUTPUT_DIR}")
    print("="*40)

if __name__ == "__main__":
    main()
