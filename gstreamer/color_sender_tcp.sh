#!/bin/bash
# GStreamer Color Stream Sender (H.264 over TCP - More Reliable)
# Usage: ./color_sender_tcp.sh <receiver_ip> <port>

IP="${1:-127.0.0.1}"
PORT="${1:-5000}"

echo "=== GStreamer Color Stream Sender (TCP) ==="
echo "Receiver IP: $IP"
echo "Port: $PORT"
echo "Press Ctrl+C to stop"
echo ""

# Use TCP sink for reliable delivery
gst-launch-1.0 -v v4l2src device=/dev/video0 \
  ! video/x-raw, width=640, height=480, framerate=30/1 \
  ! videoconvert \
  ! x264enc bitrate=2000000 speed-preset=ultrafast tune=zerolatency threads=4 \
  ! rtph264pay config-interval=1 pt=96 \
  ! srtpenc \
  ! tcpclientsink host=$IP port=$PORT
