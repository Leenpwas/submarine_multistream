# Quick Reference Card

## Files Created

```
gstreamer/
├── color_sender.sh           # UDP sender (fast)
├── color_sender_tcp.sh       # TCP sender (reliable) ⭐ USE THIS
├── color_receiver.sh         # Receiver for any stream
├── color_rtsp_server.sh      # RTSP server (pro setup)
├── all_streams_sender.sh     # Send all 4 streams
├── test_stream.sh            # Test everything
├── install.sh                # Auto-install on Pi
├── submarine-gstreamer.service  # systemd service file
├── README.md                 # Full documentation
└── QUICKSTART.md             # This file
```

---

## 30 Second Setup

### On Pi (Camera Side):
```bash
cd /home/leenos/submarine_multistream/gstreamer
./install.sh
```

Follow prompts, enter your laptop IP, enable auto-start.

### On Laptop (Receiver Side):
```bash
./color_receiver.sh 5000
```

Done! 🎉

---

## Common Commands

### Start Stream Manually
```bash
./color_sender_tcp.sh 192.168.1.100 5000
```

### Stop Auto-Start
```bash
sudo systemctl stop submarine-gstreamer
```

### Check Status
```bash
sudo systemctl status submarine-gstreamer
```

### View Logs
```bash
journalctl -u submarine-gstreamer -f
```

### Restart Service
```bash
sudo systemctl restart submarine-gstreamer
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| No video | Check camera: `ls /dev/video*` |
| Permission denied | Run: `sudo usermod -a -G video $USER` |
| Connection refused | Start receiver first |
| Laggy video | Reduce bitrate in sender script |
| High CPU | Reduce threads in x264enc |

---

## Default Ports

| Stream | Port |
|--------|------|
| Color | 5001 |
| Depth | 5002 |
| IR | 5003 |
| Map | 5004 |

---

## IP Addresses

| Role | IP |
|------|-----|
| Your laptop | 192.168.1.100 |
| Pi 2 (camera) | 192.168.1.101 |
| Pi 1 (control) | 192.168.1.102 |

*(Adjust these to match your network)*
