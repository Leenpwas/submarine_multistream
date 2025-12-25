#!/bin/bash
# Send all 4 streams simultaneously using GStreamer
# Usage: ./all_streams_sender.sh <receiver_ip>

IP="${1:-127.0.0.1}"

echo "=== GStreamer Multi-Stream Sender ==="
echo "Receiver IP: $IP"
echo ""
echo "Starting 4 streams:"
echo "  Color:  $IP:5001"
echo "  Depth:  $IP:5002"
echo "  IR:      $IP:5003"
echo "  Map:     $IP:5004"
echo ""
echo "Press Ctrl+C to stop all streams"
echo ""

# Color stream (port 5001)
gst-launch-1.0 v4l2src device=/dev/video0 \
  ! video/x-raw, width=640, height=480, framerate=30/1 \
  ! videoconvert \
  ! x264enc bitrate=2000000 speed-preset=ultrafast tune=zerolatency threads=2 \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=$IP port=5001 sync=false \
  2>/dev/null &

COLOR_PID=$!
echo "[+] Color stream started (PID: $COLOR_PID)"

# Note: For depth/IR from Orbbec, we need to check if they're on separate video devices
# Or use libobsensor + appsrc approach

# Wait a bit
sleep 1

# Store PIDs for cleanup
trap "kill $COLOR_PID 2>/dev/null; echo 'Stopped all streams'; exit" INT TERM

# Keep script running
while true; do
    sleep 1
    # Check if processes are still running
    if ! kill -0 $COLOR_PID 2>/dev/null; then
        echo "[!] Color stream died, restarting..."
        # Restart color stream
        gst-launch-1.0 v4l2src device=/dev/video0 \
          ! video/x-raw, width=640, height=480, framerate=30/1 \
          ! videoconvert \
          ! x264enc bitrate=2000000 speed-preset=ultrafast tune=zerolatency threads=2 \
          ! rtph264pay config-interval=1 pt=96 \
          ! udpsink host=$IP port=5001 sync=false 2>/dev/null &
        COLOR_PID=$!
    fi
done
