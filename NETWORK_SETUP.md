# Network Camera Streaming Setup

## Hardware Setup
- **Pi 1 (Submarine)**: Orbbec camera connected, runs `camera_sender`
- **Pi 2 (Control Station)**: Runs `camera_receiver`, displays video + 2D map
- **Network**: Connect via Ethernet (up to 100m with Cat5e/Cat6)

## Network Configuration

### Static IP Setup (Recommended)

**On Pi 1 (Submarine - Sender):**
```bash
# Edit /etc/dhcpcd.conf
sudo nano /etc/dhcpcd.conf

# Add at the end:
interface eth0
static ip_address=192.168.1.10/24
static routers=192.168.1.1
static domain_name_servers=8.8.8.8
```

**On Pi 2 (Control - Receiver):**
```bash
# Edit /etc/dhcpcd.conf
sudo nano /etc/dhcpcd.conf

# Add:
interface eth0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=8.8.8.8
```

Reboot both Pis after configuration.

## Running the System

### On Pi 2 (Receiver - Start FIRST):
```bash
cd ~/submarine_multistream/build
export LD_LIBRARY_PATH=~/OrbbecSDK/lib/arm64:$LD_LIBRARY_PATH
./camera_receiver 5000
```

### On Pi 1 (Sender):
```bash
cd ~/submarine_multistream/build
export LD_LIBRARY_PATH=~/OrbbecSDK/lib/arm64:$LD_LIBRARY_PATH
./camera_sender 192.168.1.100 5000
```

## Testing Connection
```bash
# On Pi 2, test if Pi 1 is reachable:
ping 192.168.1.10

# Check open port:
nc -zv 192.168.1.100 5000
```

## Troubleshooting

### Connection refused:
- Ensure receiver is running first
- Check firewall: `sudo ufw allow 5000`

### High latency:
- Use direct Ethernet connection (no switch/router if possible)
- Reduce frame size or framerate in sender code

### Packet loss:
- Check cable quality (use Cat5e or better)
- Check for EMI interference near motors

## Performance

- **Bandwidth**: ~30 Mbps for all streams (Depth+Color+IR)
- **Latency**: <100ms over 20m cable
- **Max distance**: 100m with standard Ethernet cable
