#!/bin/bash

# robOS ESP32S3 Standalone Flash Script
# This script can flash the firmware without requiring ESP-IDF installation

set -e

# Default configuration
DEFAULT_PORT="/dev/ttyUSB0"
DEFAULT_BAUD="460800"
CHIP="esp32s3"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}=================================="
    echo -e "robOS ESP32S3 Flash Tool"
    echo -e "==================================${NC}"
    echo ""
}

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -p, --port PORT    Serial port (default: $DEFAULT_PORT)"
    echo "  -b, --baud BAUD    Baud rate (default: $DEFAULT_BAUD)"
    echo "  -e, --erase        Erase flash before flashing"
    echo "  -m, --monitor      Start monitor after flashing"
    echo "  -h, --help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                           # Flash with default settings"
    echo "  $0 -p /dev/ttyACM0          # Use different port"
    echo "  $0 -p COM3 -b 115200        # Windows with slower baud rate"
    echo "  $0 --erase --monitor        # Erase, flash, and monitor"
    echo ""
}

check_dependencies() {
    if ! command -v esptool.py &> /dev/null; then
        echo -e "${RED}❌ Error: esptool.py not found!${NC}"
        echo ""
        echo "Please install esptool:"
        echo "  pip install esptool"
        echo ""
        echo "Or install via system package manager:"
        echo "  # Ubuntu/Debian:"
        echo "  sudo apt-get install esptool"
        echo "  # macOS:"
        echo "  brew install esptool"
        echo ""
        exit 1
    fi
}

check_files() {
    local missing_files=()
    
    if [ ! -f "flash_args" ]; then
        missing_files+=("flash_args")
    fi
    
    if [ ! -f "robOS.bin" ]; then
        missing_files+=("robOS.bin")
    fi
    
    if [ ! -f "bootloader.bin" ]; then
        missing_files+=("bootloader.bin")
    fi
    
    if [ ! -f "partition-table.bin" ]; then
        missing_files+=("partition-table.bin")
    fi
    
    if [ ${#missing_files[@]} -ne 0 ]; then
        echo -e "${RED}❌ Error: Missing required files:${NC}"
        for file in "${missing_files[@]}"; do
            echo "  - $file"
        done
        echo ""
        echo "Please ensure you have extracted the complete release package."
        exit 1
    fi
}

detect_port() {
    local detected_ports=()
    
    # Try common USB serial ports
    for port in /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.usbmodem* COM*; do
        if [ -e "$port" ] 2>/dev/null; then
            detected_ports+=("$port")
        fi
    done
    
    if [ ${#detected_ports[@]} -eq 0 ]; then
        echo -e "${YELLOW}⚠️  Warning: No USB serial ports detected${NC}"
        echo "Please ensure your ESP32S3 device is connected and try specifying the port manually."
        echo ""
        return 1
    elif [ ${#detected_ports[@]} -eq 1 ]; then
        echo -e "${GREEN}📱 Auto-detected port: ${detected_ports[0]}${NC}"
        DEFAULT_PORT="${detected_ports[0]}"
    else
        echo -e "${YELLOW}📱 Multiple ports detected:${NC}"
        for i in "${!detected_ports[@]}"; do
            echo "  $((i+1)). ${detected_ports[i]}"
        done
        echo ""
        echo -e "${YELLOW}Using first port: ${detected_ports[0]}${NC}"
        DEFAULT_PORT="${detected_ports[0]}"
    fi
}

# Parse command line arguments
PORT=""
BAUD=""
ERASE_FLASH=false
START_MONITOR=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -b|--baud)
            BAUD="$2"
            shift 2
            ;;
        -e|--erase)
            ERASE_FLASH=true
            shift
            ;;
        -m|--monitor)
            START_MONITOR=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Unknown option: $1${NC}"
            print_usage
            exit 1
            ;;
    esac
done

# Main execution
print_header

echo -e "${BLUE}🔍 Checking dependencies...${NC}"
check_dependencies

echo -e "${BLUE}📁 Checking required files...${NC}"
check_files

if [ -z "$PORT" ]; then
    echo -e "${BLUE}🔌 Detecting serial ports...${NC}"
    detect_port || true
    PORT="$DEFAULT_PORT"
fi

if [ -z "$BAUD" ]; then
    BAUD="$DEFAULT_BAUD"
fi

echo ""
echo -e "${BLUE}Configuration:${NC}"
echo "📱 Chip: $CHIP"
echo "🔌 Port: $PORT"
echo "⚡ Baud rate: $BAUD"
echo "🗑️  Erase flash: $([ "$ERASE_FLASH" = true ] && echo "Yes" || echo "No")"
echo "📺 Start monitor: $([ "$START_MONITOR" = true ] && echo "Yes" || echo "No")"
echo ""

# Erase flash if requested
if [ "$ERASE_FLASH" = true ]; then
    echo -e "${YELLOW}🗑️  Erasing flash...${NC}"
    esptool.py --chip $CHIP --port $PORT erase_flash
    echo ""
fi

# Flash the firmware
echo -e "${YELLOW}🚀 Flashing firmware...${NC}"
esptool.py --chip $CHIP --port $PORT --baud $BAUD write_flash @flash_args

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✅ Flash completed successfully!${NC}"
    echo ""
    
    if [ -f "build_info.txt" ]; then
        echo -e "${BLUE}📋 Build Information:${NC}"
        cat build_info.txt
        echo ""
    fi
    
    if [ "$START_MONITOR" = true ]; then
        echo -e "${BLUE}📺 Starting serial monitor...${NC}"
        echo -e "${YELLOW}Press Ctrl+C to exit monitor${NC}"
        echo ""
        esptool.py --chip $CHIP --port $PORT --baud 115200 monitor
    else
        echo -e "${BLUE}💡 To monitor output, run:${NC}"
        echo "esptool.py --chip $CHIP --port $PORT --baud 115200 monitor"
        echo ""
        echo -e "${BLUE}Or use this script with monitor option:${NC}"
        echo "$0 --monitor"
    fi
else
    echo ""
    echo -e "${RED}❌ Flash failed! Please check your device connection.${NC}"
    echo ""
    echo -e "${YELLOW}Troubleshooting tips:${NC}"
    echo "1. Ensure ESP32S3 is properly connected"
    echo "2. Try a different USB cable"
    echo "3. Check if the device is in download mode"
    echo "4. Try a slower baud rate: $0 -b 115200"
    echo "5. Try erasing flash first: $0 --erase"
    exit 1
fi