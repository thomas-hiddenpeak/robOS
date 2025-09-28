#!/bin/bash
# ESP32S3 Monitor Script
# This script opens serial monitor using the fixed port configuration

# Load configuration
source .esp_config

echo "🔍 正在打开ESP32S3串口监视器..."
echo "📱 设备芯片: $ESP_CHIP"
echo "🔌 使用端口: $ESP_PORT"
echo "⚡ 波特率: $ESP_BAUD"
echo ""
echo "💡 按 Ctrl+] 退出监视器"
echo ""

# Start monitor
idf.py -p $ESP_PORT monitor