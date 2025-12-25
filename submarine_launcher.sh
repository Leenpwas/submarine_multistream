#!/bin/bash
#
# Submarine Vision System Launcher
# For: Surface Laptop/Pi <-> Camera Submarine Pi (via Ethernet switch)
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default configuration
BUILD_DIR="$HOME/submarine_multistream/build"
ORBBEC_SDK="$HOME/OrbbecSDK"

# Detect architecture
if [ "$(uname -m)" = "aarch64" ]; then
    ORBBEC_LIB_DIR="arm64"
    IS_PI=true
else
    ORBBEC_LIB_DIR="linux_x64"
    IS_PI=false
fi

export LD_LIBRARY_PATH="$ORBBEC_SDK/lib/$ORBBEC_LIB_DIR:$LD_LIBRARY_PATH"

# Network defaults
SENDER_IP="${SENDER_IP:-192.168.1.10}"
RECEIVER_IP="${RECEIVER_IP:-192.168.1.100}"
PORT="${PORT:-5000}"

# Print banner
print_banner() {
    clear
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║     SUBMARINE VISION SYSTEM - Camera Streaming         ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
    echo ""
    echo -e "Network Configuration:"
    echo -e "  ${GREEN}Surface Station (Receiver):${NC} $RECEIVER_IP"
    echo -e "  ${GREEN}Camera Submarine (Sender):${NC} $SENDER_IP"
    echo -e "  ${GREEN}Port:${NC} $PORT"
    echo ""
    echo -e "Hardware: ${BLUE}$([ "$IS_PI" = true ] && echo "Raspberry Pi (ARM64)" || echo "Laptop/Desktop (x64)")${NC}"
    echo ""
}

# Check dependencies
check_dependencies() {
    echo -e "${YELLOW}Checking dependencies...${NC}"

    if [ ! -d "$ORBBEC_SDK" ]; then
        echo -e "${RED}ERROR: OrbbecSDK not found at $ORBBEC_SDK${NC}"
        echo "Please install OrbbecSDK first"
        exit 1
    fi

    if [ ! -d "$BUILD_DIR" ]; then
        echo -e "${RED}ERROR: Build directory not found${NC}"
        echo "Run: cd ~/submarine_multistream && mkdir build && cd build && cmake .. && make"
        exit 1
    fi

    echo -e "${GREEN}✓ OrbbecSDK found${NC}"
    echo -e "${GREEN}✓ Build directory exists${NC}"
    echo ""
}

# Test network connection
test_network() {
    local target_ip=$1
    echo -e "${YELLOW}Testing network connection to $target_ip...${NC}"

    if ping -c 2 -W 2 "$target_ip" &> /dev/null; then
        echo -e "${GREEN}✓ Network reachable${NC}"
        return 0
    else
        echo -e "${RED}✗ Cannot reach $target_ip${NC}"
        echo -e "${YELLOW}  Check Ethernet connection and IP addresses${NC}"
        return 1
    fi
}

# Surface Station Menu
surface_menu() {
    while true; do
        print_banner
        echo -e "${CYAN}SURFACE STATION MODE${NC}"
        echo ""
        echo "Select streaming mode:"
        echo ""
        echo -e "  ${GREEN}1${NC}) Full 4-Stream Display (Color + Depth + IR + 2D Map)"
        echo -e "  ${GREEN}2${NC}) Switchable (Press 1=Color, 2=Depth+Map)"
        echo -e "  ${GREEN}3${NC}) Color Stream Only (low bandwidth)"
        echo -e "  ${GREEN}4${NC}) Depth + 2D Map Only"
        echo -e "  ${GREEN}5${NC}) 3D Point Cloud Viewer"
        echo -e "  ${GREEN}6${NC}) Raw Camera Data (16-bit depth values)"
        echo ""
        echo -e "  ${YELLOW}N${NC}) Configure Network Settings"
        echo -e "  ${YELLOW}T${NC}) Test Network Connection"
        echo -e "  ${YELLOW}Q${NC}) Quit"
        echo ""
        read -p "Select option: " choice

        case $choice in
            1|2|3|4|5|6)
                echo ""
                echo -e "${YELLOW}Starting receiver... (Press ESC to exit)${NC}"
                echo -e "${YELLOW}Make sure sender is running on submarine first!${NC}"
                echo ""
                sleep 2

                case $choice in
                    1) "$BUILD_DIR/submarine_receiver" "$PORT" ;;
                    2) "$BUILD_DIR/udp_switchable_receiver" "$PORT" ;;
                    3) "$BUILD_DIR/color_receiver" "$PORT" ;;
                    4) "$BUILD_DIR/depth_receiver" "$PORT" ;;
                    5) "$BUILD_DIR/submarine_3d_viz" ;;
                    6) "$BUILD_DIR/camera_receiver" "$PORT" ;;
                esac
                ;;
            n|N)
                echo ""
                read -p "Enter Surface IP [$RECEIVER_IP]: " new_ip
                [ ! -z "$new_ip" ] && RECEIVER_IP="$new_ip"
                read -p "Enter Submarine IP [$SENDER_IP]: " new_ip
                [ ! -z "$new_ip" ] && SENDER_IP="$new_ip"
                read -p "Enter Port [$PORT]: " new_port
                [ ! -z "$new_port" ] && PORT="$new_port"
                ;;
            t|T)
                test_network "$SENDER_IP"
                echo ""
                read -p "Press Enter to continue..."
                ;;
            q|Q)
                echo ""
                echo -e "${GREEN}Goodbye!${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}Invalid option${NC}"
                sleep 1
                ;;
        esac
    done
}

# Submarine Camera Menu
submarine_menu() {
    while true; do
        print_banner
        echo -e "${CYAN}CAMERA SUBMARINE MODE${NC}"
        echo ""
        echo "Select streaming mode:"
        echo ""
        echo -e "  ${GREEN}1${NC}) Send All Streams (Color + Depth + IR + 2D Map)"
        echo -e "  ${GREEN}2${NC}) Switchable (Surface can switch modes)"
        echo -e "  ${GREEN}3${NC}) Color Stream Only (low bandwidth)"
        echo -e "  ${GREEN}4${NC}) Depth + 2D Map Only"
        echo -e "  ${GREEN}5${NC}) Raw Camera Data (TCP, 16-bit depth)"
        echo ""
        echo -e "  ${YELLOW}N${NC}) Configure Network Settings"
        echo -e "  ${YELLOW}T${NC}) Test Network Connection"
        echo -e "  ${YELLOW}C${NC}) Check Camera Status"
        echo -e "  ${YELLOW}Q${NC}) Quit"
        echo ""
        read -p "Select option: " choice

        case $choice in
            1|2|3|4|5)
                echo ""
                echo -e "${YELLOW}Starting sender to $RECEIVER_IP:$PORT${NC}"
                echo -e "${YELLOW}Press Ctrl+C to stop${NC}"
                echo ""
                sleep 1

                case $choice in
                    1) "$BUILD_DIR/submarine_sender" "$RECEIVER_IP" "$PORT" ;;
                    2) "$BUILD_DIR/udp_switchable_sender" "$RECEIVER_IP" "$PORT" ;;
                    3) "$BUILD_DIR/color_sender" "$RECEIVER_IP" "$PORT" ;;
                    4) "$BUILD_DIR/depth_sender" "$RECEIVER_IP" "$PORT" ;;
                    5) "$BUILD_DIR/camera_sender" "$RECEIVER_IP" "$PORT" ;;
                esac
                ;;
            n|N)
                echo ""
                read -p "Enter Surface IP [$RECEIVER_IP]: " new_ip
                [ ! -z "$new_ip" ] && RECEIVER_IP="$new_ip"
                read -p "Enter Submarine IP [$SENDER_IP]: " new_ip
                [ ! -z "$new_ip" ] && SENDER_IP="$new_ip"
                read -p "Enter Port [$PORT]: " new_port
                [ ! -z "$new_port" ] && PORT="$new_port"
                ;;
            t|T)
                test_network "$RECEIVER_IP"
                echo ""
                read -p "Press Enter to continue..."
                ;;
            c|C)
                echo ""
                echo -e "${YELLOW}Checking Orbbec camera...${NC}"
                if lsusb | grep -i orbbec &> /dev/null; then
                    echo -e "${GREEN}✓ Orbbec camera detected${NC}"
                    lsusb | grep -i orbbec
                else
                    echo -e "${RED}✗ No Orbbec camera found${NC}"
                    echo "Available USB devices:"
                    lsusb
                fi
                echo ""
                read -p "Press Enter to continue..."
                ;;
            q|Q)
                echo ""
                echo -e "${GREEN}Goodbye!${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}Invalid option${NC}"
                sleep 1
                ;;
        esac
    done
}

# Auto-start mode (for systemd)
autostart() {
    local mode=$1  # "sender" or "receiver"
    local stream_type=$2  # "all", "color", "depth", "raw"

    echo -e "${CYAN}Submarine Vision System - Auto-start${NC}"
    echo "Mode: $mode | Stream: $stream_type"
    echo ""

    case "$mode" in
        sender)
            echo "Starting sender to $RECEIVER_IP:$PORT..."
            case "$stream_type" in
                all) exec "$BUILD_DIR/submarine_sender" "$RECEIVER_IP" "$PORT" ;;
                color) exec "$BUILD_DIR/color_sender" "$RECEIVER_IP" "$PORT" ;;
                depth) exec "$BUILD_DIR/depth_sender" "$RECEIVER_IP" "$PORT" ;;
                raw) exec "$BUILD_DIR/camera_sender" "$RECEIVER_IP" "$PORT" ;;
                switchable) exec "$BUILD_DIR/udp_switchable_sender" "$RECEIVER_IP" "$PORT" ;;
                *) echo "Unknown stream type: $stream_type"; exit 1 ;;
            esac
            ;;
        receiver)
            echo "Starting receiver on port $PORT..."
            case "$stream_type" in
                all) exec "$BUILD_DIR/submarine_receiver" "$PORT" ;;
                color) exec "$BUILD_DIR/color_receiver" "$PORT" ;;
                depth) exec "$BUILD_DIR/depth_receiver" "$PORT" ;;
                raw) exec "$BUILD_DIR/camera_receiver" "$PORT" ;;
                switchable) exec "$BUILD_DIR/udp_switchable_receiver" "$PORT" ;;
                *) echo "Unknown stream type: $stream_type"; exit 1 ;;
            esac
            ;;
        *)
            echo "Usage: $0 autostart [sender|receiver] [all|color|depth|raw|switchable]"
            exit 1
            ;;
    esac
}

# Main menu
main_menu() {
    check_dependencies

    while true; do
        print_banner
        echo "Select your role:"
        echo ""
        echo -e "  ${GREEN}1${NC}) Surface Station (Laptop/Desktop - Receive streams)"
        echo -e "  ${GREEN}2${NC}) Camera Submarine (Pi with Orbbec camera - Send streams)"
        echo ""
        echo -e "  ${YELLOW}H${NC}) Help & Documentation"
        echo -e "  ${YELLOW}Q${NC}) Quit"
        echo ""
        read -p "Select option: " choice

        case $choice in
            1)
                surface_menu
                ;;
            2)
                submarine_menu
                ;;
            h|H)
                clear
                cat << 'EOF'
╔══════════════════════════════════════════════════════════╗
║              SUBMARINE VISION SYSTEM - HELP              ║
╚══════════════════════════════════════════════════════════╝

NETWORK SETUP:
  Surface Station:    192.168.1.100
  Camera Submarine:   192.168.1.10
  Port:               5000

  Connect both via Ethernet to a switch.

QUICK START:
  1. On Surface: Select option 1, then choose streaming mode
  2. On Submarine: Select option 2, then choose streaming mode
  3. Receiver MUST start before sender!

STREAMING MODES:
  • Full 4-Stream: All data (Color, Depth, IR, 2D Map)
  • Switchable:    Surface controls which stream to receive
  • Color Only:     Low bandwidth, good for slow networks
  • Depth + Map:    For navigation and obstacle avoidance
  • Raw Data:       16-bit depth values for processing

CONTROLS:
  • ESC: Exit stream viewer
  • In Switchable mode:
    - Press '1' for Color stream
    - Press '2' for Depth + 2D Map

TROUBLESHOOTING:
  • No connection: Check Ethernet cables and IP addresses
  • High latency:  Use direct connection, reduce resolution
  • Glitchy video: Use Raw Data or TCP modes
  • Camera not found: Check USB connection

FOR AUTO-START ON BOOT:
  Create systemd service:
    ExecStart=/path/to/submarine_launcher.sh autostart sender all
EOF
                echo ""
                read -p "Press Enter to continue..."
                ;;
            q|Q)
                echo ""
                echo -e "${GREEN}Goodbye!${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}Invalid option${NC}"
                sleep 1
                ;;
        esac
    done
}

# Command line args
case "${1:-}" in
    autostart)
        autostart "$2" "$3"
        ;;
    surface)
        check_dependencies
        surface_menu
        ;;
    submarine)
        check_dependencies
        submarine_menu
        ;;
    *)
        main_menu
        ;;
esac
