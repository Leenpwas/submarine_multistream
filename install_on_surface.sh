#!/bin/bash
# Installation script for Surface Pi (Receiver)
# Run this on the surface Raspberry Pi

echo "=== Submarine Vision Receiver Installation ==="
echo ""

# Create directory
mkdir -p /home/pi/submarine_multistream/build

# Copy the built executable
echo "Copy these files from your laptop to the Pi:"
echo "  - submarine_multistream/build/submarine_vision_receiver"
echo ""

# Update service file (no IP needed for receiver)
cat > /tmp/submarine-vision-receiver.service << 'EOF'
[Unit]
Description=Submarine Vision Receiver - Displays depth/2D/3D data
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/submarine_multistream/build
ExecStart=/home/pi/submarine_multistream/build/submarine_vision_receiver 5000
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Install systemd service
echo "Installing systemd service..."
sudo cp /tmp/submarine-vision-receiver.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable submarine-vision-receiver.service

echo ""
echo "=== Installation Complete ==="
echo ""
echo "To start the receiver (with window):"
echo "  sudo systemctl start submarine-vision-receiver"
echo ""
echo "OR run manually to see the window:"
echo "  /home/pi/submarine_multistream/build/submarine_vision_receiver 5000"
echo ""
echo "To check status:"
echo "  sudo systemctl status submarine-vision-receiver"
echo ""
echo "To view logs:"
echo "  journalctl -u submarine-vision-receiver -f"
echo ""
