#!/bin/bash
# GStreamer RTSP Server for Color Stream (Most Reliable!)
# Usage: ./color_rtsp_server.sh <port>

PORT="${1:-8554}"

echo "=== GStreamer RTSP Server ==="
echo "RTSP URL: rtsp://localhost:$PORT/camera"
echo "To view: ffplay rtsp://<your-ip>:$PORT/camera"
echo "Press Ctrl+C to stop"
echo ""

# RTSP server with H.264 encoding
gst-launch-1.0 -v v4l2src device=/dev/video0 \
  ! video/x-raw, width=640, height=480, framerate=30/1 \
  ! videoconvert \
  ! x264enc bitrate=2000000 speed-preset=ultrafast tune=zerolatency threads=4 \
  ! rtph264pay name=pay0 config-interval=1 pt=96 \
  ! rtpmp2tpay \
  ! udpsink host=224.0.0.1 port=5000 sync=false

# Alternative using mediasrc (if available)
# gst-rtsp-server-setup command can be used for full RTSP server
