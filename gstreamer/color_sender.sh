#!/bin/bash
# GStreamer Color Stream Sender (H.264 over UDP/RTP)
# Usage: ./color_sender.sh <receiver_ip> <port>

IP="${1:-127.0.0.1}"
PORT="${2:-5000}"

echo "=== GStreamer Color Stream Sender ==="
echo "Receiver IP: $IP"
echo "Port: $PORT"
echo "Press Ctrl+C to stop"
echo ""

# Try different encoders in order of preference
# 1. OpenH264 (available on most systems)
# 2. Theora (free, always available)
# 3. VP8 (free, good quality)

gst-launch-1.0 -v v4l2src device=/dev/video0 \
  ! video/x-raw, width=640, height=480, framerate=30/1 \
  ! videoconvert \
  ! openh264enc bitrate=2000000 \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=$IP port=$PORT sync=false
