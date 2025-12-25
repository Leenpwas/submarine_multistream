# 🚢 Submarine Vision - Web Interface

**Live underwater video with AI object detection - accessible from any web browser!**

## System Architecture

```
Pi (Submarine)              Laptop (Web Server)
─────────────              ──────────────────
LIGHTWEIGHT                POWERFUL ML
Orbbec Camera              TensorFlow Detection
    ↓                            ↓
JPEG Encoding           Object Detection + Bounding Boxes
    ↓                            ↓
TCP Stream              Flask Web Server
    ↓                            ↓
Send to Laptop ───────→ HTTP/MJPEG Stream
                                  ↓
                          Your Web Browser
                                  ↓
                          View + Control Anywhere
```

**Pi barely works** - just captures and sends!
**Laptop does all the heavy ML processing!** 💪

---

## Features

### 🎥 Live Video Streaming
- Real-time color video from submarine
- Accessible from ANY device with a web browser
- Works over local network or internet
- Low latency (~100ms)

### 🤖 AI Object Detection (on Laptop)
- Detects **80+ object types**:
  - 👤 People, swimmers, divers
  - 🚤 Boats, ships
  - 🐟 Marine life
  - 🪑 Floating debris, obstacles
  - 📦 Objects, equipment

### 📊 Real-Time Display
- **Green bounding boxes** around detected objects
- **Confidence scores** (e.g., "person: 0.95")
- **Live statistics**: FPS, object count, bandwidth
- **Auto-refreshing** object list

### 🎮 Browser Controls
- **Toggle Detection** - Turn AI on/off
- **📸 Take Snapshot** - Save images instantly
- Works on **phone, tablet, laptop**

---

## Quick Setup

### 1. On Your Laptop

```bash
cd /home/leenos/submarine_multistream

# Download ML models (23 MB, one-time)
chmod +x download_models.sh
./download_models.sh

# Build programs
cd build
cmake ..
make web_sender

# Install Flask (if not installed)
pip3 install flask
```

### 2. Start Web Server on Laptop

```bash
cd /home/leenos/submarine_multistream
python3 web_server.py
```

You'll see:
```
🚢 SUBMARINE VISION WEB SERVER
====================================
✓ ML Detection: ENABLED

📡 Access the web interface at:
   → http://localhost:5000
   → http://<your-laptop-ip>:5000
```

### 3. Open Web Browser

Go to: **http://localhost:5000**

You should see the web interface waiting for video!

### 4. On Raspberry Pi

```bash
cd ~/submarine_multistream/build
sudo ./web_sender 192.168.2.3 5001
```

Replace `192.168.2.3` with your laptop's IP!

---

## Access From Anywhere

### Local Network
- Access at: `http://<your-laptop-ip>:5000`
- Works on any device connected to your WiFi

### Over Internet
**Option 1: Port Forwarding (Recommended)**
1. Login to your router (usually `192.168.1.1`)
2. Find "Port Forwarding" or "NAT"
3. Forward external port `5000` → internal `5000` (your laptop)
4. Access from anywhere: `http://<your-public-ip>:5000`

**Option 2: Ngrok (Easiest, No Router Config)**
```bash
# Install ngrok
curl -s https://ngrok-agent.s3.amazonaws.com/ngrok.asc | sudo tee /etc/apt/trusted.gpg.d/ngrok.asc >/dev/null
echo "deb https://ngrok-agent.s3.amazonaws.com buster main" | sudo tee /etc/apt/sources.list.d/ngrok.list
sudo apt update && sudo apt install ngrok

# Start tunnel
ngrok http 5000
```

You'll get a URL like: `https://abc123.ngrok.io`
Access it from ANYWHERE! 🌍

---

## Usage

### Web Interface

**Stats Panel:**
- **FPS** - Frames per second
- **Objects** - Number of detected objects
- **Bandwidth** - Data usage

**Controls:**
- **🤖 Toggle Detection** - Turn object detection on/off
- **📸 Take Snapshot** - Download current frame as JPG

**Objects Panel:**
- Shows all detected objects in real-time
- Auto-refreshes every 500ms
- Example: "person: 0.95", "boat: 0.87"

---

## Network Configuration

### For Local Testing
```
Pi (192.168.2.2)        Laptop (192.168.2.3)
───────────────         ─────────────────
Run web_sender          Run web_server
Connects to laptop       Serves web interface
```

### For Internet Access
```
Internet
    ↓
Router (Port Forward 5000)
    ↓
Laptop (192.168.2.3:5000)
    ↓
Web Browser (Anywhere)
```

---

## Troubleshooting

### "No models found" on laptop?
```bash
cd /home/leenos/submarine_multistream
./download_models.sh
```

### Pi can't connect to laptop?
```bash
# Test from Pi
ping 192.168.2.3

# Check if web server is running on laptop
netstat -tlnp | grep 5001
```

### Browser shows "Connection refused"?
```bash
# Check if web server is running
netstat -tlnp | grep 5000

# Allow firewall
sudo ufw allow 5000/tcp
```

### Can't access from internet?
- Make sure port 5000 is forwarded on router
- Check your public IP at: https://whatismyipaddress.com
- Try accessing from phone on cellular data

---

## Performance

| Metric | Value |
|--------|-------|
| Video Resolution | 640x480 |
| Pi CPU Usage | ~5% (just captures!) |
| Laptop CPU Usage | ~30-50% (ML detection) |
| FPS | 15-30 |
| Bandwidth | ~500 Kbps |
| Latency | <200ms |

---

## File Structure

```
submarine_multistream/
├── web_server.py              # Laptop web server (Flask)
├── web_sender.cpp             # Pi sender
├── download_models.sh         # Download ML models
├── build/
│   ├── web_sender             # Compiled Pi program
│   ├── frozen_inference_graph.pb
│   ├── ssd_mobilenet_v2_coco.pbtxt
│   └── coco_classes.txt
└── OrbbecSDK_minimal/         # Camera SDK
```

---

## Advanced

### Run on Boot (Pi - Sealed Submarine)

Create systemd service on Pi:
```bash
sudo nano /etc/systemd/system/submarine-web-sender.service
```

```ini
[Unit]
Description=Submarine Web Sender
After=network.target

[Service]
Type=simple
User=krs
WorkingDirectory=/home/krs/submarine_multistream/build
ExecStart=/usr/bin/sudo /home/krs/submarine_multistream/build/web_sender 192.168.2.3 5001
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl enable submarine-web-sender
```

Now Pi auto-starts streaming on boot! 🚀

---

## Support

For issues:
1. Check network connectivity
2. Verify models are downloaded
3. Confirm camera is connected
4. Check browser console for errors

---

**Enjoy your submarine vision system!** 🚢📡🤖
