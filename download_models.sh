#!/bin/bash
set -e

echo "=== Downloading ML Object Detection Models ==="
echo ""

# Create models directory
mkdir -p models
cd models

echo "[1/4] Downloading MobileNet SSD COCO model..."
wget -q --show-progress \
  https://github.com/opencv/opencv_zoo/blob/master/models/dnn/object_detection/frozen_inference_graph.pb?raw=true \
  -O frozen_inference_graph.pb

echo ""
echo "[2/4] Downloading config file..."
wget -q --show-progress \
  https://github.com/opencv/opencv_extra/blob/master/testdata/dnn/ssd_mobilenet_v2_coco_2018_03_29.pbtxt?raw=true \
  -O ssd_mobilenet_v2_coco.pbtxt

echo ""
echo "[3/4] Downloading COCO class names..."
wget -q --show-progress \
  https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names \
  -O coco_classes.txt

echo ""
echo "[4/4] Copying to build directory..."
cp frozen_inference_graph.pb ../build/
cp ssd_mobilenet_v2_coco.pbtxt ../build/
cp coco_classes.txt ../build/

echo ""
echo "✓ Models downloaded successfully!"
echo ""
echo "Model files:"
echo "  - frozen_inference_graph.pb (22.6 MB)"
echo "  - ssd_mobilenet_v2_coco.pbtxt (53 KB)"
echo "  - coco_classes.txt (625 bytes)"
echo ""
echo "Can detect 80 object types including:"
echo "  person, car, boat, bicycle, dog, cat, chair, etc."
echo ""
