#!/bin/bash
# Quick local test - runs both sender and receiver on your laptop

echo "=== Submarine Vision System - Local Test ==="
echo ""
echo "Starting receiver..."
echo ""

# Start receiver in background
./submarine_vision_receiver 5000 &
RECEIVER_PID=$!

sleep 2
echo "Receiver started (PID: $RECEIVER_PID)"
echo ""
echo "Starting sender..."
echo ""

# Start sender in background
./submarine_vision_sender 127.0.0.1 5000 &
SENDER_PID=$!

echo "Sender started (PID: $SENDER_PID)"
echo ""
echo "============================================"
echo "  System Running - Window should be open!"
echo "============================================"
echo ""
echo "Controls:"
echo "  Arrow keys (← ↑ → ↓) - Rotate 3D view"
echo "  +/- - Zoom in/out"
echo "  ESC - Exit"
echo ""
echo "Press Ctrl+C to stop both programs"
echo ""

# Wait for user to stop
wait
