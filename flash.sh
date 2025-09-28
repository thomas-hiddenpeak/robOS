#!/bin/bash
# ESP32S3 Flash Script
# This script uses the fixed port configuration for ESP32S3 device

# Load configuration
source .esp_config

echo "🚀 正在烧录robOS到ESP32S3设备..."
echo "📱 设备芯片: $ESP_CHIP"
echo "🔌 使用端口: $ESP_PORT"
echo "⚡ 波特率: $ESP_BAUD"
echo ""

# Flash the firmware
idf.py -p $ESP_PORT flash

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ 烧录完成！robOS已成功烧录到ESP32S3设备"
    echo "💡 现在可以打开串口监视器查看输出: idf.py -p $ESP_PORT monitor"
else
    echo ""
    echo "❌ 烧录失败！请检查设备连接"
    exit 1
fi