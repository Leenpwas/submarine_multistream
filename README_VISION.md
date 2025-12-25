# Submarine Vision System - TCP Streaming

## Overview
A network streaming system that captures depth data from an Orbbec camera on a submarine Pi and displays it on a surface Pi with three views:
- **Depth Visualization**: Colored heatmap of depth data
- **2D Navigation Map**: Bird's-eye view showing obstacles
- **3D Point Cloud**: Interactive 3D visualization

## Quick Start (Test on Laptop)

### 1. Build
```bash
cd /home/leenos/submarine_multistream/build
cmake ..
make submarine_vision_sender submarine_vision_receiver
```

### 2. Run Test
```bash
./test_local.sh
```

Or manually in two terminals:

**Terminal 1 (Receiver):**
```bash
./submarine_vision_receiver 5000
```

**Terminal 2 (Sender):**
```bash
./submarine_vision_sender 127.0.0.1 5000
```

### 3. Controls
- **Arrow keys** (← ↑ → ↓): Rotate 3D point cloud
- **+/-**: Zoom in/out
- **ESC**: Exit

## Deployment on Raspberry Pis

### Submarine Pi (Sender - Sealed Unit)

The submarine Pi will auto-start on boot, so it needs to be configured before sealing.

#### Installation:
```bash
# Copy files from your laptop to the Pi
scp build/submarine_vision_sender pi@submarine.local:~/
scp install_on_submarine.sh pi@submarine.local:~/

# SSH into the submarine Pi
ssh pi@submarine.local

# Run the installer
./install_on_submarine.sh

# Enter Surface Pi IP when prompted (e.g., 192.168.1.100)

# Reboot to test
sudo reboot
```

The sender will automatically start streaming on boot!

### Surface Pi (Receiver)

#### Installation:
```bash
# Copy files from your laptop to the Pi
scp build/submarine_vision_receiver pi@surface.local:~/
scp install_on_surface.sh pi@surface.local:~/

# SSH into the surface Pi
ssh pi@surface.local

# Run the installer
./install_on_surface.sh

# Start the receiver (with GUI window)
/home/pi/submarine_multistream/build/submarine_vision_receiver 5000
```

## Network Configuration

Both Pis must be connected to the same Ethernet switch:

```
┌──────────────┐         ┌─────────────┐         ┌──────────────┐
│  Submarine   │────────►│   Switch    │◄────────│    Surface   │
│      Pi      │         │             │         │      Pi      │
│  (Sender)    │         │  Ethernet   │         │  (Receiver)  │
└──────────────┘         └─────────────┘         └──────────────┘
   Sealed                     │                      Display + Controls
   Underwater                 │
                              ▼
                        (Your Network)
```

### Recommended Static IPs:
- Submarine Pi: `192.168.1.50`
- Surface Pi: `192.168.1.100`
- Port: `5000`

## System Requirements

### Submarine Pi:
- Raspberry Pi 4 (recommended) or Pi 3B+
- Orbbec depth camera (Astra Pro, Femto Bolt, etc.)
- Ethernet connection
- OrbbecSDK installed

### Surface Pi:
- Raspberry Pi 4 (recommended)
- HDMI display connected
- Ethernet connection
- OpenCV installed

## Performance

| Metric | Value |
|--------|-------|
| Frame Rate | ~20 FPS |
| Bandwidth | ~3-5 Mbps |
| Latency | <100ms |
| 3D Points | 60,000+ per frame |
| Resolution | 640x480 (depth) |

## Troubleshooting

### No connection between Pis?
```bash
# Check network connectivity
ping 192.168.1.100

# Check if sender is running
systemctl status submarine-vision-sender

# Check logs
journalctl -u submarine-vision-sender -f
```

### Black screen on receiver?
- Wait 5-10 seconds for data to stream
- Check camera is connected to submarine Pi
- Verify OrbbecSDK is installed

### Can't rotate 3D view?
- Make sure the receiver window has focus (click on it)
- Try different arrow keys
- Check terminal for key code messages

### Service won't start?
```bash
# Check detailed logs
journalctl -u submarine-vision-sender -n 50

# Verify executable exists
ls -la /home/pi/submarine_multistream/build/submarine_vision_sender

# Check permissions
chmod +x /home/pi/submarine_multistream/build/submarine_vision_sender
```

## Advanced Usage

### Change port:
```bash
# Receiver
./submarine_vision_receiver 8888

# Sender
./submarine_vision_sender 192.168.1.100 8888
```

### View frame statistics:
The receiver terminal will show:
```
✓ Received frame 30 (DEPTH_VIS)
✓ Received frame 60 (3D_DEPTH - 65432 points)
```

### Save 2D views to disk:
Add this to receiver code to save frames:
```cpp
cv::imwrite("/tmp/depth_vis.jpg", depthVis);
cv::imwrite("/tmp/2d_map.jpg", map2D);
```

## File Structure

```
submarine_multistream/
├── submarine_vision_sender.cpp       # Main sender program
├── submarine_vision_receiver.cpp     # Main receiver program
├── install_on_submarine.sh           # Auto-install script (submarine)
├── install_on_surface.sh             # Auto-install script (surface)
├── test_local.sh                     # Quick local test script
├── submarine-vision-sender.service   # systemd service (auto-start)
└── DEPLOYMENT.md                     # Full deployment guide
```

## Architecture

### Sender (Submarine Pi):
```
Orbbec Camera (DEPTH stream)
    ↓
Capture Frame (with mutex)
    ↓
┌─────────────────────────────────┐
│  Processing Pipeline:           │
│  1. Depth Visualization (JET)   │
│  2. 2D Map Generation           │
│  3. Raw Depth (16-bit)          │
└─────────────────────────────────┘
    ↓
TCP Socket (to Surface Pi:5000)
```

### Receiver (Surface Pi):
```
TCP Socket (receive from Submarine)
    ↓
Decode JPEG/PNG Frames
    ↓
┌─────────────────────────────────┐
│  Display:                       │
│  - Top-left: Depth Vis (640x360)│
│  - Top-right: 2D Map (640x360)  │
│  - Bottom: 3D Point Cloud (1280x360) │
└─────────────────────────────────┘
    ↓
OpenCV Window (Interactive)
```

## Network Protocol

**TCP Header (12 bytes):**
```
[Frame ID (4)][Frame Type (4)][Data Size (4)]
```

**Frame Types:**
- `1` = Depth Visualization (JPEG)
- `2` = 2D Navigation Map (JPEG)
- `3` = 3D Depth Data (PNG, preserves 16-bit)

## Support

For issues or questions:
1. Check DEPLOYMENT.md for detailed deployment steps
2. Check system logs: `journalctl -u submarine-vision-* -f`
3. Test locally first using `test_local.sh`
