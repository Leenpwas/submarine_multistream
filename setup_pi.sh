#!/bin/bash
set -e

echo "=== Submarine Multi-Stream Setup for Raspberry Pi ==="
echo ""

# Install dependencies
echo "Installing dependencies..."
sudo apt update
sudo apt install -y build-essential cmake git \
    libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev

# Setup OrbbecSDK
if [ ! -d "$HOME/OrbbecSDK" ]; then
    echo "Cloning OrbbecSDK..."
    cd ~
    git clone https://github.com/orbbec/OrbbecSDK.git
else
    echo "OrbbecSDK already exists"
fi

# USB permissions
echo "Setting up USB permissions..."
sudo cp ~/OrbbecSDK/99-obsensor-libusb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger

# Build
echo "Building submarine_multistream..."
cd ~/submarine_multistream
mkdir -p build && cd build
cmake ..
make -j$(nproc)

echo ""
echo "=== Setup Complete! ==="
echo ""
echo "To run:"
echo "  cd ~/submarine_multistream/build"
echo "  export LD_LIBRARY_PATH=~/OrbbecSDK/lib/arm64:\$LD_LIBRARY_PATH"
echo "  ./submarine_multistream"
echo ""
