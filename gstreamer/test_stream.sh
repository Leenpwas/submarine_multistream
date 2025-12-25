#!/bin/bash
# Test GStreamer streaming locally

echo "=========================================="
echo "  GStreamer Stream Test"
echo "=========================================="
echo ""

# Check if GStreamer is installed
if ! command -v gst-launch-1.0 &> /dev/null; then
    echo "❌ GStreamer not found!"
    echo "Install with: sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly"
    exit 1
fi

echo "✅ GStreamer found: $(gst-launch-1.0 --version)"
echo ""

# Check video devices
echo "📷 Video devices:"
ls -la /dev/video* 2>/dev/null || echo "❌ No video devices found"
echo ""

# Test camera access
echo "🧪 Testing camera access..."
timeout 3 gst-launch-1.0 v4l2src device=/dev/video0 ! "video/x-raw, width=640, height=480" ! fakesink 2>&1 | grep -q "Pipeline is PREROLLED"
if [ $? -eq 0 ]; then
    echo "✅ Camera accessible"
else
    echo "❌ Camera not accessible or wrong device"
    echo "Try: sudo usermod -a -G video $USER"
    echo "Then logout and login again"
    exit 1
fi
echo ""

# Check network
echo "🌐 Network test:"
ping -c 1 127.0.0.1 > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ Network working"
else
    echo "❌ Network issues detected"
fi
echo ""

# Start test
echo "=========================================="
echo "Starting test stream..."
echo "Sender: localhost:5000"
echo "Press Ctrl+C to stop"
echo "=========================================="
echo ""

# Start sender in background
./color_sender.sh 127.0.0.1 5000 &
SENDER_PID=$!

sleep 2

# Check if sender is running
if ps -p $SENDER_PID > /dev/null; then
    echo "✅ Sender started (PID: $SENDER_PID)"
    echo ""
    echo "To view stream, open another terminal and run:"
    echo "  ./color_receiver.sh 5000"
    echo ""
    echo "Or use VLC:"
    echo "  vlc udp://@:5000"
    echo ""

    # Wait for user to stop
    wait $SENDER_PID
else
    echo "❌ Sender failed to start"
    exit 1
fi
