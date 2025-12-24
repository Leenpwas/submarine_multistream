# Submarine Multi-Stream Vision System

Enhanced version of Orbbec MultiStream with real-time 2D navigation map for submarine/ROV applications.

## Features

### Four Real-Time Windows:
1. **IR Stream** (top-left) - Infrared camera
2. **Color Stream** (top-right) - RGB camera  
3. **Depth Stream** (bottom-left) - Depth sensing
4. **2D Navigation Map** (bottom-right) - Top-down obstacle map

### 2D Map Features:
- Real-time obstacle detection (0.2m - 4m range)
- Top-down bird's eye view
- Grid overlay for distance reference
- Green robot icon showing position
- Red/blue gradient showing obstacle distance

## Requirements

### Hardware:
- Orbbec Astra Pro Plus camera
- Raspberry Pi 4/5 (ARM64) or x86_64 Linux laptop
- USB 2.0/3.0 connection

### Software:
- Ubuntu 20.04+ or Raspberry Pi OS
- CMake 3.10+
- OpenGL, GLFW3

## Quick Start

### On Laptop (Development):
```bash
# Clone this repo
git clone <your-repo-url> submarine_multistream
cd submarine_multistream

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
export LD_LIBRARY_PATH=~/OrbbecSDK/lib/linux_x64:$LD_LIBRARY_PATH
./submarine_multistream
```

### On Raspberry Pi:
```bash
# Run setup script
cd ~/submarine_multistream
chmod +x setup_pi.sh
./setup_pi.sh

# Run
cd ~/submarine_multistream/build
export LD_LIBRARY_PATH=~/OrbbecSDK/lib/arm64:$LD_LIBRARY_PATH
./submarine_multistream
```

## Controls
- **ESC**: Exit application

## Configuration

Edit `submarine_multistream.cpp` to adjust:
- `max_range`: Detection range (line 18, default 4.0m)
- Map size: Constructor (default 640x480)
- Grid spacing: Lines 31-41
- Color schemes: Lines 75-78

## Troubleshooting

### Camera not detected:
```bash
lsusb | grep Orbbec
sudo chmod 666 /dev/bus/usb/*/*
```

### Build errors:
- Ensure OrbbecSDK is at `~/OrbbecSDK`
- Install: `sudo apt install libglfw3-dev libgl1-mesa-dev`

### Performance on Raspberry Pi:
- Reduce map resolution in constructor
- Increase subsampling (line 50: change `+= 4` to `+= 8`)

## License
Same as OrbbecSDK

## Credits
Based on Orbbec SDK Sample-MultiStream
Enhanced with 2D mapping for underwater robotics
