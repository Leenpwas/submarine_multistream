#!/bin/bash
#
# Kill all submarine vision processes
# Use this if camera gets stuck or you need to reset
#

echo "Killing all submarine processes..."

pkill -9 -f "submarine" 2>/dev/null
pkill -9 -f "ob_viewer" 2>/dev/null
pkill -9 -f "DepthViewer" 2>/dev/null

sleep 1

# Verify nothing is left
REMAINING=$(ps aux | grep -E "(submarine|ob_viewer)" | grep -v grep | wc -l)

if [ "$REMAINING" -eq 0 ]; then
    echo "✓ All processes killed"
    echo ""
    echo "Camera is now free to use"
else
    echo "⚠ Some processes still running:"
    ps aux | grep -E "(submarine|ob_viewer)" | grep -v grep
fi
