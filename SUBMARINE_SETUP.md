# Submarine Vision System - Setup Guide

## Network Topology

```
Surface Laptop/Pi (192.168.1.100)
         │
         │ Ethernet Cable (up to 100m)
         ▼
       Switch
         │
    ┌────┴────┐
    │         │
    ▼         ▼
Pi 1 (Camera) Pi 2 (BlueOS)
192.168.1.10    192.168.1.11
Orbbec Camera  Submarine Control
```

---

## Quick Start (2 Minutes)

### On Surface Station

```bash
cd ~/submarine_multistream
./submarine_launcher.sh
```

Select: `1) Surface Station` → `1) Full 4-Stream Display`

### On Camera Submarine (Pi 1)

```bash
cd ~/submarine_multistream
./submarine_launcher.sh
```

Select: `2) Camera Submarine` → `1) Send All Streams`

---

## Configure Static IPs

### Surface Station (192.168.1.100)

```bash
sudo nano /etc/dhcpcd.conf
```

Add:
```
interface eth0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=8.8.8.8
```

Reboot: `sudo reboot`

### Camera Submarine Pi 1 (192.168.1.10)

Same as above, change IP to `192.168.1.10/24`

### BlueOS Submarine Pi 2 (192.168.1.11)

Same as above, change IP to `192.168.1.11/24`

---

## Auto-Start on Boot (Camera Submarine)

Install the systemd service:

```bash
# Copy service file
sudo cp ~/submarine_multistream/submarine_launcher.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload

# Enable auto-start on boot
sudo systemctl enable submarine-camera.service

# Start now (for testing)
sudo systemctl start submarine-camera.service

# Check status
sudo systemctl status submarine-camera.service
```

View logs:
```bash
journalctl -u submarine-camera -f
```

---

## Launcher Usage

### Interactive Mode

```bash
./submarine_launcher.sh
```

**Surface Station Options:**
- `1` - Full 4-Stream Display (Color + Depth + IR + 2D Map)
- `2` - Switchable (Press 1=Color, 2=Depth+Map)
- `3` - Color Stream Only
- `4` - Depth + 2D Map Only
- `5` - 3D Point Cloud Viewer
- `6` - Raw Camera Data (16-bit depth)

**Camera Submarine Options:**
- `1` - Send All Streams
- `2` - Switchable (Surface controls mode)
- `3` - Color Stream Only
- `4` - Depth + 2D Map Only
- `5` - Raw Camera Data (TCP)

### Direct Launch (Skip Menu)

**Surface:**
```bash
# Receive all streams
./submarine_launcher.sh surface

# Or specific mode
./submarine_launcher.sh autostart receiver all
```

**Camera Submarine:**
```bash
# Send all streams
./submarine_launcher.sh submarine

# Or specific mode
./submarine_launcher.sh autostart sender all
```

### Auto-Start Modes (for systemd)

```bash
./submarine_launcher.sh autostart <sender|receiver> <all|color|depth|raw|switchable>
```

Examples:
```bash
# Send all streams (default)
./submarine_launcher.sh autostart sender all

# Receive all streams
./submarine_launcher.sh autostart receiver all

# Color only (low bandwidth)
./submarine_launcher.sh autostart sender color

# Raw depth data (for processing)
./submarine_launcher.sh autostart sender raw

# Switchable mode
./submarine_launcher.sh autostart sender switchable
```

---

## Custom Network Configuration

### Method 1: Environment Variables

```bash
export RECEIVER_IP="192.168.1.50"
export SENDER_IP="192.168.1.5"
export PORT="6000"
./submarine_launcher.sh
```

### Method 2: Edit Launcher Script

Open `submarine_launcher.sh` and change defaults around line 19-21:

```bash
SENDER_IP="${SENDER_IP:-192.168.1.10}"
RECEIVER_IP="${RECEIVER_IP:-192.168.1.100}"
PORT="${PORT:-5000}"
```

### Method 3: Interactive (Inside Launcher)

Select `N) Configure Network Settings` from menu

---

## Stream Comparison

| Mode | Bandwidth | Latency | Reliability | Use Case |
|------|-----------|---------|-------------|----------|
| Full 4-Stream | ~30 Mbps | <100ms | High | Development, testing |
| Switchable | ~15 Mbps | <100ms | High | Production, bandwidth saving |
| Color Only | ~5 Mbps | <50ms | High | Visual navigation only |
| Depth + Map | ~10 Mbps | <100ms | High | Obstacle avoidance |
| Raw Data | ~20 Mbps | <150ms | Very High | Computer vision, SLAM |

---

## Troubleshooting

### No Connection

1. **Check network:**
   ```bash
   ping 192.168.1.10  # From surface
   ping 192.168.1.100 # From submarine
   ```

2. **Verify IPs:**
   ```bash
   ip addr show
   ```

3. **Check firewall:**
   ```bash
   sudo ufw allow 5000/tcp
   sudo ufw allow 5000/udp
   ```

4. **Receiver must start FIRST!**

### Camera Not Detected

```bash
# Check USB
lsusb | grep -i orbbec

# Check permissions
ls -l /dev/bus/usb/*/*

# Add udev rules if needed
sudo cp ~/OrbbecSDK/99-obsensor-libusb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

### High Latency

- Use direct Ethernet (no switch if possible)
- Reduce resolution in sender code
- Use "Color Only" mode
- Check for EMI interference near motors

### Glitchy Video

- Use Raw Data mode (TCP instead of UDP)
- Check cable quality (Cat5e or better)
- Reduce frame rate

### Auto-Start Not Working

```bash
# Check service status
sudo systemctl status submarine-camera.service

# View logs
journalctl -u submarine-camera -n 50

# Test manually
~/submarine_multistream/submarine_launcher.sh autostart sender all
```

---

## Testing Before Deployment

### Test 1: Local Loopback

On one machine:
```bash
# Terminal 1
./submarine_launcher.sh autostart receiver all

# Terminal 2
./submarine_launcher.sh autostart sender all
```

### Test 2: Network Test

```bash
# On submarine
./submarine_launcher.sh autostart sender all

# On surface
./submarine_launcher.sh autostart receiver all
```

### Test 3: 24-Hour Stress Test

```bash
# On submarine (auto-restart on error)
while true; do
    ./submarine_launcher.sh autostart sender all
    sleep 2
done
```

---

## File Locations

| File | Location |
|------|----------|
| Launcher Script | `~/submarine_multistream/submarine_launcher.sh` |
| Systemd Service | `~/submarine_multistream/submarine_launcher.service` |
| Executables | `~/submarine_multistream/build/` |
| Logs | `journalctl -u submarine-camera` |
| 2D Map Output | `~/submarine_multistream/build/remote_2d_map.png` |

---

## Default IP Address Reference

| Device | IP Address | Role |
|--------|------------|------|
| Surface Station | 192.168.1.100 | Receiver/Display |
| Camera Submarine | 192.168.1.10 | Sender (Orbbec) |
| BlueOS Submarine | 192.168.1.11 | Submarine Control |

**Default Port:** 5000

---

## Performance Tips

### For Low Bandwidth (< 10 Mbps)
- Use "Color Only" mode
- Reduce JPEG quality in sender code (line 21: `cv::IMWRITE_JPEG_QUALITY, 80` → `60`)
- Use 640x480 resolution

### For Low Latency (< 50ms)
- Use direct Ethernet connection
- Use "Color Only" mode
- Reduce frame rate to 15 FPS

### For High Reliability
- Use "Raw Data" mode (TCP)
- Use auto-start with systemd
- Add watchdog timer

### For Battery Power
- Use "Switchable" mode
- Send only when needed
- Reduce frame rate

---

## Getting Help

```bash
# Show help in launcher
./submarine_launcher.sh
# Then press 'H'

# Check camera status
./submarine_launcher.sh
# Then select 'C) Check Camera Status'
```

---

**Ready for deployment! 🌊**
