# Submarine Vision - ML Object Detection System

Streams **color video** from submarine Pi over **Internet** with **AI object detection**!

## Features

- 🎥 **Color video streaming** from Orbbec camera
- 🌐 **Internet streaming** - access from anywhere
- 🤖 **Object detection** with 80+ object types (people, boats, fish, etc.)
- 📊 **Real-time GUI** with bounding boxes and confidence scores
- ⚡ **15-30 FPS** performance

## Quick Setup

### 1. On Laptop (Surface)

```bash
cd /home/leenos/submarine_multistream

# Download ML models (23 MB)
chmod +x download_models.sh
./download_models.sh

# Build
cd build
cmake ..
make ml_receiver internet_color_sender

# Find your public IP
# Visit: https://whatismyipaddress.com
# Let's say it's: 203.0.113.50
```

### 2. Forward Port on Router

Access your router (usually `192.168.1.1`) and forward:
- **External Port:** 5000
- **Internal Port:** 5000
- **Internal IP:** Your laptop's IP (e.g., 192.168.2.3)
- **Protocol:** TCP

Or use `ngrok` (no router config needed):
```bash
# Install ngrok
curl -s https://ngrok-agent.s3.amazonaws.com/ngrok.asc | sudo tee /etc/apt/trusted.gpg.d/ngrok.asc >/dev/null
echo "deb https://ngrok-agent.s3.amazonaws.com buster main" | sudo tee /etc/apt/sources.list.d/ngrok.list
sudo apt update && sudo apt install ngrok

# Start tunnel
ngrok tcp 5000
```

You'll get an address like: `tcp://0.tcp.ngrok.io:12345`

### 3. Run Receiver on Laptop

```bash
cd /home/leenos/subipeline_multistream/build
./ml_receiver 5000
```

### 4. On Raspberry Pi (Submarine)

```bash
cd ~/submarine_multistream/build
```

**Option A - Direct to public IP:**
```bash
sudo ./internet_color_sender 203.0.113.50 5000
```

**Option B - Via ngrok:**
```bash
sudo ./internet_color_sender 0.tcp.ngrok.io 12345
```

## How It Works

```
Submarine Pi                    Internet                 Laptop
───────────                    ────────                 ──────
Orbbec Camera                                             GUI
    ↓                                                        ↓
Color Frame → JPEG → TCP → �────→ Router → ──────→ ML Model
    ↓                                                        ↓
Compressed (80% quality)                            Object Detection
                                                          ↓
                                                     Bounding Boxes
                                                          ↓
                                                    Display with Labels
```

## Object Detection

Can detect **80 types** of objects including:

- 👤 **People** - person, human
- 🚤 **Vehicles** - car, boat, bicycle, motorcycle, bus, truck
- 🐟 **Animals** - dog, cat, bird, horse, sheep, cow, fish
- 🪑 **Objects** - chair, couch, bottle, cup, laptop, phone

Each detection shows:
- **Bounding box** (green rectangle)
- **Label** (object type)
- **Confidence score** (0-100%)

## GUI Controls

- **Arrow Keys**: Not applicable (automatic detection)
- **ESC**: Exit
- **Close Window**: Exit

## Performance

| Metric | Value |
|--------|-------|
| Resolution | 640x480 |
| FPS | 15-30 |
| Bandwidth | ~500 Kbps |
| Latency | <200ms |
| Detection | Real-time |

## Troubleshooting

### No connection?

**Check port forwarding:**
```bash
# On laptop, check if listening:
netstat -tlnp | grep 5000

# Test from Pi:
telnet your-public-ip 5000
```

**Check firewall:**
```bash
sudo ufw status
sudo ufw allow 5000/tcp
```

### Detection not working?

**Download models:**
```bash
./download_models.sh
```

Models should be in `build/` directory:
- `frozen_inference_graph.pb` (22.6 MB)
- `ssd_mobilenet_v2_coco.pbtxt` (53 KB)
- `coco_classes.txt` (625 bytes)

### Low FPS?

- Reduce JPEG quality: Change `80` to `70` in `internet_color_sender.cpp`
- Lower resolution: Change `640x480` to `320x240`
- Use faster model: MobileNet SSD is already optimized

## Advanced Usage

### Custom Model

Replace MobileNet SSD with your own model:

1. Train model (TensorFlow/PyTorch)
2. Convert to TensorFlow format
3. Update paths in `ml_receiver.cpp`

### Add More Classes

Edit `coco_classes.txt` to add custom object types.

### Save Detected Frames

Add to `ml_receiver.cpp`:
```cpp
if (!boxes.empty()) {
    cv::imwrite(cv::format("detection_%04d.jpg", frameCount), frame);
}
```

## Network Options

### Option 1: Direct Internet (Fastest)
- Port forward on router
- Direct IP connection
- Lowest latency

### Option 2: Ngrok (Easiest)
- No router config
- Automatic tunnel
- Slightly higher latency

### Option 3: VPN (Most Secure)
- WireGuard/OpenVPN
- Encrypted connection
- Good for sensitive operations

## File Structure

```
submarine_multistream/
├── internet_color_sender.cpp    # Pi sender
├── ml_receiver.cpp              # Laptop receiver with AI
├── download_models.sh           # Download ML models
├── build/
│   ├── internet_color_sender    # Compiled sender
│   ├── ml_receiver              # Compiled receiver
│   ├── frozen_inference_graph.pb
│   ├── ssd_mobilenet_v2_coco.pbtxt
│   └── coco_classes.txt
└── OrbbecSDK_minimal/           # Camera SDK
```

## Support

For issues:
1. Check network connectivity
2. Verify port forwarding
3. Confirm models are downloaded
4. Check camera is connected

## License

Uses OpenCV and TensorFlow models (Apache 2.0 License).
