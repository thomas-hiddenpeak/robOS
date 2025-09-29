# ESP32S3 快速操作指南

## 设备配置
ESP32S3设备端口已固定配置为: `/dev/cu.usbmodem01234567891`

配置文件位置: `.esp_config`

## 快速操作命令

### 1. 编译项目
```bash
idf.py build
```

### 2. 烧录固件
```bash
# 使用便捷脚本（推荐）
./flash.sh

# 或者直接使用idf.py
idf.py -p /dev/cu.usbmodem01234567891 flash
```

### 3. 串口监视器
```bash
# 使用便捷脚本（推荐）
./monitor.sh

# 或者直接使用idf.py
idf.py -p /dev/cu.usbmodem01234567891 monitor
```

### 4. 编译并烧录
```bash
idf.py build && ./flash.sh
```

### 5. 烧录并打开监视器
```bash
./flash.sh && ./monitor.sh
```

## 注意事项
- ESP32S3设备端口已固定，无需每次手动指定
- 按 `Ctrl+]` 退出串口监视器
- 如果烧录失败，请检查设备连接和端口是否正确