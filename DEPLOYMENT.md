# Submarine Vision System - Deployment Guide

## Overview
- **Submarine Pi** (sealed underwater): Captures camera data and streams to surface
- **Surface Pi**: Receives and displays depth visualization, 2D map, and 3D point cloud

## Build on Your Laptop First

```bash
cd /home/leenos/submarine_multistream/build
cmake ..
make submarine_vision_sender submarine_vision_receiver
```

## Deploy to Raspberry Pis

### Step 1: Copy Files to Pis

Copy to **Submarine Pi**:
```bash
scp build/submarine_vision_sender pi@submarine.local:~/
scp install_on_submarine.sh pi@submarine.local:~/
scp submarine-vision-sender.service pi@submarine.local:~/
```

Copy to **Surface Pi**:
```bash
scp build/submarine_vision_receiver pi@surface.local:~/
scp install_on_surface.sh pi@surface.local:~/
```

### Step 2: Install on Submarine Pi (Sealed Unit)

SSH into submarine Pi:
```bash
ssh pi@submarine.local
cd ~
./install_on_submarine.sh
```

When prompted, enter the **Surface Pi IP address** (e.g., `192.168.1.100`)

### Step 3: Install on Surface Pi

SSH into surface Pi:
```bash
ssh pi@surface.local
cd ~
./install_on_surface.sh
```

### Step 4: Start the System

**On Surface Pi** (start first):
```bash
# Option A: Auto-start on boot
sudo systemctl start submarine-vision-receiver

# Option B: Run manually (to see the window)
/home/pi/submarine_multistream/build/submarine_vision_receiver 5000
```

**On Submarine Pi** (will auto-start on reboot):
```bash
sudo systemctl start submarine-vision-sender
```

Or just **reboot the submarine Pi** - it will auto-start!

## Testing Before Deployment

### Quick Test on Your Laptop:

**Terminal 1 (Receiver)**:
```bash
cd /home/leenos/submarine_multistream/build
./submarine_vision_receiver 5000
```

**Terminal 2 (Sender)**:
```bash
cd /home/leenos/submarine_multistream/build
./submarine_vision_sender 127.0.0.1 5000
```

## Network Setup

Both Pis connect to the same Ethernet switch:

```
┌──────────────┐     Ethernet     ┌─────────────┐
│  Submarine   │◄────────────────►│   Switch    │
│      Pi      │                  │             │
└──────────────┘                  └─────────────┘
                                          │
                                          │ Ethernet
                                          ▼
                                  ┌─────────────┐
                                  │  Surface    │
                                  │     Pi      │
                                  └─────────────┘
```

**Recommended Static IPs:**
- Submarine Pi: `192.168.1.50`
- Surface Pi: `192.168.1.100`

## Useful Commands

### Check if services are running:
```bash
systemctl status submarine-vision-sender
systemctl status submarine-vision-receiver
```

### View logs:
```bash
journalctl -u submarine-vision-sender -f
journalctl -u submarine-vision-receiver -f
```

### Restart services:
```bash
sudo systemctl restart submarine-vision-sender
sudo systemctl restart submarine-vision-receiver
```

### Disable auto-start (if needed):
```bash
sudo systemctl disable submarine-vision-sender
sudo systemctl disable submarine-vision-receiver
```

## Controls (Surface Pi Window)

- **Arrow keys** (← ↑ → ↓): Rotate 3D point cloud
- **+/-**: Zoom in/out
- **ESC**: Exit

## Troubleshooting

### No connection?
- Check both Pis are on the same network
- Ping from submarine: `ping 192.168.1.100`
- Check firewall: `sudo ufw disable` (if needed)

### Black screen?
- Wait a few seconds for data to stream
- Check camera is connected to submarine Pi

### Service won't start?
- Check logs: `journalctl -u submarine-vision-sender -n 50`
- Make sure OrbbecSDK is installed on the Pi
- Make sure executable permissions are set: `chmod +x submarine_vision_sender`

## Performance

- **Frame rate**: ~20 FPS
- **Bandwidth**: ~3-5 Mbps
- **Latency**: <100ms (local network)
- **3D Points**: 60,000+ per frame
