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
    cp -r "$SCRIPT_DIR/OrbbecSDK_minimal" "$HOME/OrbbecSDK"
    echo "✓ OrbbecSDK installed to $HOME/OrbbecSDK"
elif [ -d "$HOME/OrbbecSDK" ]; then
    echo "✓ OrbbecSDK already exists at $HOME/OrbbecSDK"
else
    echo "✗ OrbbecSDK not found!"
    exit 1
fi

# Build
echo "[4/5] Building..."
mkdir -p build && cd build
# Use simplified CMakeLists that only builds vision system
cp ../CMakeLists_vision.txt ./CMakeLists.txt
cmake .
make submarine_vision_sender submarine_vision_receiver -j4

# Setup launchers
echo "[5/5] Creating launch scripts..."
cat > "$HOME/run_receiver.sh" << 'RUNNER'
#!/bin/bash
cd ~/submarine_multistream/build
./submarine_vision_receiver 5000
RUNNER

cat > "$HOME/run_sender.sh" << 'RUNNER'
#!/bin/bash
SURFACE_IP="192.168.1.100"
cd ~/submarine_multistream/build
./submarine_vision_sender $SURFACE_IP 5000
RUNNER
chmod +x "$HOME/run_receiver.sh" "$HOME/run_sender.sh"

echo ""
echo "=== Setup Complete! ==="
echo ""
echo "Quick Start:"
echo "  Receiver: ~/run_receiver.sh"
echo "  Sender:   ~/run_sender.sh"
echo ""
echo "Username: $(whoami)"
echo "Home: $HOME"
echo ""