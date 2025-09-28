#!/bin/bash

# Config Manager Unit Test Runner
# This script builds and runs the unit tests for the config_manager component

set -e

echo "======================================="
echo "Config Manager Unit Test Runner"
echo "======================================="

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Set up ESP-IDF environment
if [ -z "$IDF_PATH" ]; then
    echo "Setting up ESP-IDF environment..."
    source ~/esp/v5.5.1/esp-idf/export.sh
fi

echo "Building tests..."
idf.py build

if [ $? -eq 0 ]; then
    echo ""
    echo "Tests built successfully!"
    echo ""
    
    # Check if a serial port is provided as argument
    if [ $# -eq 1 ]; then
        PORT=$1
        echo "Flashing and running tests on $PORT..."
        idf.py -p $PORT flash monitor
    else
        echo "To run tests on hardware, provide the serial port:"
        echo "  $0 <PORT>"
        echo ""
        echo "Example:"
        echo "  $0 /dev/cu.usbmodem01234567891"
        echo ""
        echo "Or manually run:"
        echo "  idf.py -p <PORT> flash monitor"
        echo ""
    fi
else
    echo "Build failed!"
    exit 1
fi