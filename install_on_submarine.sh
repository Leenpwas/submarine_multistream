#!/bin/bash
# Installation script for Submarine Pi (Sender)
# Run this on the submarine Raspberry Pi

echo "=== Submarine Vision Sender Installation ==="
echo ""

# Update the IP address in the service file
SURFACE_IP="192.168.1.100"
echo "Enter Surface Pi IP address (default: $SURFACE_IP):"
read -r INPUT_IP
if [ -n "$INPUT_IP" ]; then
    SURFACE_IP="$INPUT_IP"
fi

echo ""
echo "Installing to: /home/pi/submarine_multistream"
echo "Will connect to: $SURFACE_IP:5000"
echo ""

# Create directory
mkdir -p /home/pi/submarine_multistream/build

# Copy the built executable (assuming you've built it on your laptop first)
echo "Copy these files from your laptop to the Pi:"
echo "  - submarine_multistream/build/submarine_vision_sender"
echo "  - submarine_multistream/submarine-vision-sender.service"
echo ""

# Update service file with correct IP
sed "s/192.168.1.100/$SURFACE_IP/g" submarine-vision-sender.service > /tmp/submarine-vision-sender.service

# Install systemd service
echo "Installing systemd service..."
sudo cp /tmp/submarine-vision-sender.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable submarine-vision-sender.service

echo ""
echo "=== Installation Complete ==="
echo ""
echo "To start the service now:"
echo "  sudo systemctl start submarine-vision-sender"
echo ""
echo "To check status:"
echo "  sudo systemctl status submarine-vision-sender"
echo ""
echo "To view logs:"
echo "  journalctl -u submarine-vision-sender -f"
echo ""
echo "The sender will automatically start on boot!"
echo ""
