#!/bin/bash
set -e

echo "=== Submarine Vision System - Pi 5 Auto Setup ==="
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Update system
echo "[1/5] Updating system..."
sudo apt update && sudo apt upgrade -y

# Install dependencies
echo "[2/5] Installing dependencies..."
sudo apt install -y build-essential cmake git pkg-config libopencv-dev python3-opencv libeigen3-dev libusb-1.0-0-dev

# Setup OrbbecSDK
echo "[3/5] Installing OrbbecSDK (58MB)..."
if [ -d "$SCRIPT_DIR/OrbbecSDK_minimal" ]; then
    cp -r "$SCRIPT_DIR/OrbbecSDK_minimal" /home/pi/OrbbecSDK
    echo "✓ OrbbecSDK installed"
elif [ -d "/home/pi/OrbbecSDK" ]; then
    echo "✓ OrbbecSDK already exists"
else
    echo "✗ OrbbecSDK not found!"
    exit 1
fi

# Build
echo "[4/5] Building..."
mkdir -p build && cd build
cmake ..
make submarine_vision_sender submarine_vision_receiver -j4

# Setup launchers
echo "[5/5] Creating launch scripts..."
cat > /home/pi/run_receiver.sh << 'RUNNER'
#!/bin/bash
cd ~/submarine_multistream/build
./submarine_vision_receiver 5000
RUNNER

cat > /home/pi/run_sender.sh << 'RUNNER'
#!/bin/bash
SURFACE_IP="192.168.1.100"
cd ~/submarine_multistream/build
./submarine_vision_sender $SURFACE_IP 5000
RUNNER
chmod +x /home/pi/run_receiver.sh /home/pi/run_sender.sh

echo "=== Setup Complete! ==="
echo "Receiver: /home/pi/run_receiver.sh"
echo "Sender:   /home/pi/run_sender.sh"
