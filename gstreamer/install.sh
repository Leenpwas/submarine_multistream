#!/bin/bash
# Installation script for GStreamer submarine streaming
# Run this on the Raspberry Pi (camera side)

set -e

echo "=========================================="
echo "  Submarine GStreamer Setup"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "❌ Don't run as root!"
    echo "Run as normal user (pi)"
    exit 1
fi

# Update package list
echo "📦 Updating package list..."
sudo apt update

# Install GStreamer and plugins
echo "📦 Installing GStreamer..."
sudo apt install -y \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-doc \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    gstreamer1.0-gl \
    gstreamer1.0-alsa \
    gstreamer1.0-pulseaudio

# Install useful tools
echo "📦 Installing additional tools..."
sudo apt install -y \
    vlc \
    ffmpeg \
    netcat \
    htop

echo ""
echo "✅ Installation complete!"
echo ""

# Add user to video group
echo "🔧 Adding user to video group..."
sudo usermod -a -G video $USER
echo "✅ Added to video group"
echo "⚠️  You need to logout and login again for this to take effect!"
echo ""

# Test GStreamer
echo "🧪 Testing GStreamer..."
gst-launch-1.0 --version
echo ""

# Check camera
echo "📷 Checking camera..."
if [ -e /dev/video0 ]; then
    echo "✅ Camera found at /dev/video0"
    ls -l /dev/video0
else
    echo "⚠️  No camera found at /dev/video0"
    echo "Connect your Orbbec camera and run: sudo systemctl restart udev"
fi
echo ""

# Ask for receiver IP
echo "=========================================="
echo "Configuration"
echo "=========================================="
echo ""
read -p "Enter your laptop/receiver IP (default: 192.168.1.100): " RECEIVER_IP
RECEIVER_IP=${RECEIVER_IP:-192.168.1.100}

read -p "Enter port (default: 5000): " PORT
PORT=${PORT:-5000}

echo ""
echo "Configuration saved:"
echo "  Receiver IP: $RECEIVER_IP"
echo "  Port: $PORT"
echo ""

# Update systemd service with user's IP
echo "🔧 Creating systemd service..."
sed "s/192.168.1.100/$RECEIVER_IP/g" submarine-gstreamer.service > /tmp/submarine-gstreamer.service
sudo mv /tmp/submarine-gstreamer.service /etc/systemd/system/submarine-gstreamer.service

# Enable service
sudo systemctl daemon-reload
echo "✅ Systemd service installed"
echo ""

# Ask about auto-start
read -p "Enable auto-start on boot? (y/n): " AUTOSTART
if [ "$AUTOSTART" = "y" ] || [ "$AUTOSTART" = "Y" ]; then
    sudo systemctl enable submarine-gstreamer.service
    echo "✅ Auto-start enabled"
    echo ""
    echo "To start now: sudo systemctl start submarine-gstreamer.service"
else
    echo "Auto-start not enabled"
    echo "To enable later: sudo systemctl enable submarine-gstreamer.service"
fi
echo ""

echo "=========================================="
echo "Installation Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo ""
echo "1. Logout and login again (for video group)"
echo ""
echo "2. Test the stream:"
echo "   ./test_stream.sh"
echo ""
echo "3. If test works, enable auto-start:"
echo "   sudo systemctl start submarine-gstreamer.service"
echo ""
echo "4. On your laptop, run receiver:"
echo "   ./color_receiver.sh $PORT"
echo ""
echo "For troubleshooting, see README.md"
echo ""
echo "=========================================="
