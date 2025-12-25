#!/bin/bash
# Setup script for Raspberry Pi 5

echo "=== Submarine Vision System Setup ==="
echo ""

# Update system
echo "Step 1: Updating system..."
sudo apt update && sudo apt upgrade -y

# Install dependencies
echo ""
echo "Step 2: Installing dependencies..."
sudo apt install -y build-essential cmake git pkg-config libopencv-dev python3-opencv

# Check for OrbbecSDK
echo ""
echo "Step 3: Checking for OrbbecSDK..."
if [ -d "$HOME/OrbbecSDK" ]; then
    echo "✓ OrbbecSDK found!"
else
    echo "✗ OrbbecSDK NOT found!"
    echo ""
    echo "Please copy OrbbecSDK to your home directory:"
    echo "  scp -r /path/to/OrbbecSDK pi@$(hostname).local:~/"
    echo ""
    echo "Then run this script again."
    exit 1
fi

# Clone repository
echo ""
echo "Step 4: Downloading submarine vision system..."
cd ~
if [ -d "submarine_multistream" ]; then
    echo "Repository already exists, pulling latest..."
    cd submarine_multistream
    git pull
else
    git clone https://github.com/Leenpwas/submarine_multistream.git
    cd submarine_multistream
fi

# Build
echo ""
echo "Step 5: Building..."
mkdir -p build
cd build
cmake ..
make -j4

echo ""
echo "=== Setup Complete! ==="
echo ""
echo "To run:"
echo "  Receiver (Surface Pi): ./submarine_vision_receiver 5000"
echo "  Sender (Submarine Pi):  ./submarine_vision_sender <surface_ip> 5000"
echo ""
