# robOS ESP32S3 Firmware

这是为ESP32S3设备编译的robOS固件包。

## 📦 包含文件

- `robOS.bin` - 主应用程序二进制文件
- `bootloader.bin` - ESP32S3引导加载程序
- `partition-table.bin` - 分区表
- `flash_args` - esptool刷写参数
- `flash_standalone.sh` - 独立刷写脚本
- `build_info.txt` - 构建信息和详细说明

## 🚀 快速刷写

### 方法一：使用自动化脚本（推荐）

```bash
# 赋予执行权限
chmod +x flash_standalone.sh

# 自动检测端口并刷写
./flash_standalone.sh

# 或指定端口
./flash_standalone.sh -p /dev/ttyUSB0

# 擦除后刷写并启动监视器
./flash_standalone.sh --erase --monitor
```

### 方法二：手动刷写

1. 安装esptool（如果尚未安装）：
   ```bash
   pip install esptool
   ```

2. 连接ESP32S3设备并刷写：
   ```bash
   esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 write_flash @flash_args
   ```

## 🔧 常见端口

- **Linux**: `/dev/ttyUSB0`, `/dev/ttyACM0`
- **macOS**: `/dev/cu.usbserial-*`, `/dev/cu.usbmodem-*`
- **Windows**: `COM3`, `COM4`, 等

## 📺 监视输出

刷写完成后，你可以监视串口输出：

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 115200 monitor
```

或使用脚本的监视器选项：
```bash
./flash_standalone.sh --monitor
```

## 🛠️ 故障排除

如果刷写失败，请尝试：

1. **检查连接**: 确保ESP32S3正确连接到计算机
2. **更换USB线**: 使用数据传输线，而非仅充电线
3. **降低波特率**: 使用 `-b 115200` 参数
4. **擦除flash**: 使用 `--erase` 选项
5. **进入下载模式**: 按住BOOT按钮，然后按RESET按钮

## 📋 系统要求

- Python 3.6+
- esptool 包
- USB转串口驱动（如需要）

## 🔗 更多信息

更多详细信息请参考项目主页: https://github.com/thomas-hiddenpeak/robOS