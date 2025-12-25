#!/bin/bash
# GStreamer Color Stream Receiver
# Usage: ./color_receiver.sh <port>

PORT="${1:-5000}"

echo "=== GStreamer Color Stream Receiver ==="
echo "Listening on port: $PORT"
echo "Press Ctrl+C to stop"
echo ""

gst-launch-1.0 -v udpsrc port=$PORT \
  ! application/x-rtp, media=video, encoding-name=H264, payload=96 \
  ! rtph264depay \
  ! avdec_h264 \
  ! videoconvert \
  ! autovideosink sync=false
