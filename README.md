# RM-01 robOS 板上机架操作系统 (重构版)

## 🚀 项目概览

这是RM-01板上机架操作系统的全面重构版本，采用完全模块化的组件架构设计。每个功能都是独立的组件，支持单独开发、测试和维护。

### 核心设计理念
- **完全模块化**: 每个功能都是独立组件
- **控制台为核心**: 统一的命令行交互接口
- **事件驱动**: 组件间通过事件系统通信
- **平台级组件化**: 连硬件抽象层也是独立组件
- **可扩展性优先**: 为未来功能扩展预留接口

## 📚 项目文档

- **[项目进度记录](docs/PROJECT_PROGRESS.md)** - 详细的开发进度和完成状态
- **[技术架构文档](docs/TECHNICAL_ARCHITECTURE.md)** - 系统架构设计和技术决策
- **[API参考文档](docs/API_REFERENCE.md)** - 完整的API接口文档
- **[代码规范](docs/CODING_STANDARDS.md)** - 开发规范和最佳实践

## ⭐ 项目状态

- ✅ **事件管理组件** - 9个测试用例全部通过
- ✅ **硬件抽象层组件** - 5个测试用例全部通过
- ✅ **控制台核心组件** - 8个测试用例全部通过
- ✅ **配置管理组件** - 统一NVS配置管理，支持多种数据类型
- ✅ **风扇控制组件** - 完整PWM控制，温度曲线，配置持久化
- 🚧 **LED控制组件** - 待开发
- 📋 **系统监控组件** - 待开发

## 🏗️ 项目架构

### 组件化架构
```
robOS/
├── components/                    # 独立功能组件
│   ├── console_core/             # 控制台核心组件
│   ├── hardware_hal/             # 硬件抽象层组件
│   ├── config_manager/           # 配置管理组件
│   ├── fan_controller/           # 风扇控制组件
│   ├── led_controller/           # LED控制组件
│   ├── ethernet_manager/         # 以太网管理组件
│   ├── storage_manager/          # 存储管理组件
│   ├── power_monitor/            # 电源监控组件
│   ├── device_manager/           # 设备管理组件
│   ├── system_monitor/           # 系统监控组件
│   └── event_manager/            # 事件管理组件
├── main/                         # 主应用程序
├── docs/                         # 项目文档
└── tools/                        # 开发工具
```

### 组件职责
- **console_core**: UART接口、命令解析器、帮助系统
- **hardware_hal**: GPIO、PWM、SPI、ADC、UART等底层接口抽象
- **config_manager**: 统一NVS配置管理、多数据类型支持、自动提交
- **fan_controller**: PWM风扇控制、温度曲线、多模式运行、配置持久化
- **led_controller**: 板载LED、触控LED、LED矩阵控制和色彩校正
- **ethernet_manager**: W5500控制、DHCP服务器、网关功能
- **storage_manager**: TF卡管理、文件系统操作、NVS配置管理
- **power_monitor**: 电压监测、电源芯片通信、功率监控
- **device_manager**: AGX、Orin、N305等设备电源控制和状态监控
- **system_monitor**: ESP32S3系统状态、内存使用、温度监控
- **event_manager**: 事件驱动的组件间通信和状态同步机制

## 板上机柜设备

1. 板载 LED GPIO 42，28颗WS2812阵列
2. 触控 LED GPIO 45，1颗状态指示WS2812
3. 矩阵 LED GPIO 9 32x32 WS2812矩阵 (1024颗LED)
4. GPIO 41 控制风扇PWM信号 25kHz PWM频率，10位分辨率
5. USB MUX控制 ,GPIO 8 - USB MUX1选择控制,GPIO 48 - USB MUX2选择控制
6. 以太网控制器 (W5500) 
7. TF卡存储 
8. 供电电压监测
9. 供电芯片


## 主要功能

### 风扇控制功能
- **PWM引脚**: GPIO 41 - 风扇PWM控制信号
- **PWM规格**: 25kHz频率，10位分辨率 (0-1023)
- **控制模式**:
  - **手动模式** (`manual`): 直接设置风扇转速百分比
  - **自动温度模式** (`auto`): 基于系统温度传感器自动调节 (待完整实现)
  - **自定义曲线模式** (`curve`): 基于用户定义的温度曲线控制
  - **关闭模式** (`off`): 风扇完全停止
- **温度曲线**: 支持2-10个温度控制点，线性插值
- **配置持久化**: 所有设置自动保存到NVS闪存
- **错误恢复**: 自动检测并恢复LEDC初始化错误

#### 工作模式详细说明

**1. 手动模式 (manual)**
- 风扇以固定转速运行
- 用户直接设置转速百分比 (0-100%)
- 不受温度影响，适合测试和固定负载场景

**2. 自动温度模式 (auto)** 
- 🚧 **开发状态**: 接口已定义，等待温度传感器集成
- **设计原理**: 根据系统实际温度传感器读数自动调节转速
- **预期功能**: 使用内置温度曲线，温度高时转速增加，温度低时转速降低
- **当前限制**: 需要集成ESP32S3内部温度传感器或外部温度传感器

**3. 自定义曲线模式 (curve)**
- 根据用户定义的温度-转速曲线控制
- 支持2-10个温度控制点，线性插值计算中间值
- 可使用测试温度进行调试: `test temp <温度值>`
- 配置持久化，重启后自动恢复
- 最灵活的控制方式，适合精确温度管理

**4. 关闭模式 (off)**
- 风扇完全停止，PWM输出为0
- 可用于节能模式或维护时关闭风扇

#### 风扇控制命令详解

**基本状态查看**
```bash
fan status              # 显示所有风扇状态
fan                     # 同上，显示风扇状态概览
```

**风扇速度控制**
```bash
fan set <fan_id> <speed>   # 设置手动速度 (0-100%)
fan set 0 75              # 设置风扇0为75%速度
```

**风扇模式控制**
```bash
fan mode <fan_id> <mode>   # 设置工作模式
fan mode 0 manual         # 手动模式 - 固定转速
fan mode 0 auto           # 自动温度模式 - 根据系统温度自动调节(预留)
fan mode 0 curve          # 自定义曲线模式 - 用户定义温度曲线
fan mode 0 off            # 关闭风扇
```

**风扇开关控制**
```bash
fan enable <fan_id> <state>  # 启用/禁用风扇
fan enable 0 on             # 启用风扇0
fan enable 0 off            # 禁用风扇0
```

**GPIO引脚配置**
```bash
fan gpio <fan_id> <pin> [channel]  # 配置GPIO和PWM通道
fan gpio 0 41                      # 配置风扇0使用GPIO41
fan gpio 0 41 1                   # 配置风扇0使用GPIO41，PWM通道1
```

**配置管理**
```bash
fan config save [fan_id]     # 保存配置到NVS（包含运行状态）
fan config save              # 保存所有风扇配置
fan config save 0            # 保存风扇0配置

fan config load [fan_id]     # 从NVS加载配置
fan config load              # 加载所有风扇配置
fan config load 0            # 加载风扇0配置

fan config show [fan_id]     # 显示当前配置
fan config show              # 显示所有风扇配置概览
fan config show 0            # 显示风扇0详细配置
```

**温度曲线配置**
```bash
fan config curve <fan_id> <temp1:speed1> [temp2:speed2] ...
# 配置温度曲线（2-10个控制点）
fan config curve 0 30:20 50:30 70:40 80:100
# 设置风扇0曲线：30°C->20%, 50°C->30%, 70°C->40%, 80°C->100%
```

**帮助信息**
```bash
fan help                # 显示完整命令参考
```

#### 配置显示格式

**状态概览** (`fan status`)：
```
Fan Controller Status:
======================
Fan 0: Enabled, Auto-Curve, Speed: 45%, Temp: 32.5°C
```

**详细配置** (`fan config show 0`)：
```
Fan 0:
  Hardware Configuration:
    GPIO Pin: 41
    PWM Channel: 0
    PWM Timer: 0
    PWM Inverted: No
  Current Status:
    Mode: Auto-Curve
    Speed: 45%
    Enabled: Yes
    Temperature: 32.5°C
    Fault: No
  Temperature Curve:
    Enabled: Yes (4 points)
    30.0°C -> 20%
    50.0°C -> 30%
    70.0°C -> 40%
    80.0°C -> 100%
```

#### 使用场景示例

**场景1：基础手动控制**
```bash
fan gpio 0 41           # 配置GPIO41
fan enable 0 on         # 启用风扇
fan set 0 60           # 设置60%转速
fan config save        # 保存配置
```

**场景2：温度曲线自动控制**
```bash
fan config curve 0 30:20 50:30 70:40 80:100  # 配置温度曲线
fan mode 0 curve                             # 启用曲线模式
test temp 45                                 # 设置测试温度45°C（调试用）
fan config save                             # 保存配置
```

**场景3：自动温度模式（待完整实现）**
```bash
# 当温度传感器集成完成后的使用方式：
fan mode 0 auto                             # 启用自动温度模式
fan config curve 0 30:20 50:30 70:40 80:100 # 设置系统默认温度曲线
fan config save                             # 保存配置
# 系统将根据实际温度传感器读数自动调节风扇转速
```

**场景4：系统重启后自动恢复**
```bash
# 系统重启后会自动：
# 1. 从NVS加载保存的配置
# 2. 恢复GPIO引脚配置
# 3. 恢复工作模式和转速
# 4. 恢复温度曲线设置
# 5. 恢复启用/禁用状态
```

#### 开发状态说明

**✅ 已完成功能**
- 手动模式控制 - 完全实现
- 自定义曲线模式 - 完全实现，支持测试温度
- GPIO配置和PWM控制 - 完全实现
- 配置持久化 - 完全实现
- 错误恢复机制 - 完全实现

**🚧 开发中功能**
- 自动温度模式 - 接口已定义，需要集成温度传感器
  - 当前使用固定温度值 (25°C)
  - 需要集成ESP32S3内部温度传感器或外部传感器
  - 预计在系统监控组件完成后集成

**🔧 技术细节**
- 使用 `test temp <值>` 命令可以模拟温度输入进行调试
- 曲线模式已可完全替代自动温度模式的功能
- 所有模式都支持线性插值算法，保证平滑的转速变化
- `fan config` 和 `fan help` 命令正确返回成功状态码

### USB MUX控制
- **MUX1引脚**: GPIO 8 - USB MUX1选择控制
- **MUX2引脚**: GPIO 48 - USB MUX2选择控制
- 支持切换USB-C接口连接目标：
  - **ESP32S3**: mux1=0, mux2=0 (默认)
  - **AGX**: mux1=1, mux2=0
  - **N305**: mux1=1, mux2=1
- 控制台命令: `usbmux esp32s3|agx|n305|status`


### 以太网功能 (W5500)
- **SPI接口**: SPI2_HOST
- **引脚配置**:
  - RST: GPIO 39 - 复位信号
  - INT: GPIO 38 - 中断信号  
  - MISO: GPIO 13 - SPI数据输入
  - SCLK: GPIO 12 - SPI时钟
  - MOSI: GPIO 11 - SPI数据输出
  - CS: GPIO 10 - SPI片选
- **网络配置**:
  - 默认IP: 10.10.99.97
  - 子网掩码: 255.255.255.0
  - 网关: 10.10.99.100
  - DNS: 8.8.8.8
- **DHCP服务器**:
  - IP池范围: 10.10.99.100 - 10.10.99.110
  - 默认租期: 24小时
  - 支持客户端跟踪和管理
- **功能特性**:
  - 静态IP配置
  - DHCP服务器功能
  - NVS配置持久化


  ### TF卡存储功能 (SDMMC 4-bit)
- **接口类型**: SDMMC 4-bit模式
- **时钟频率**: 40MHz (高速模式)
- **引脚配置**:
  - CMD: GPIO 4 - 命令线
  - CLK: GPIO 5 - 时钟线
  - D0: GPIO 6 - 数据线0
  - D1: GPIO 7 - 数据线1
  - D2: GPIO 15 - 数据线2
  - D3: GPIO 16 - 数据线3
- **文件系统**: FAT32格式
- **功能特性**:
  - 自动挂载和卸载
  - 文件和目录完整操作
  - 空间监控和管理
  - 格式化支持
  - 19个专用控制台命令
  - POSIX文件操作兼容


### 电源监控功能
- **供电电压监测**: GPIO 18 (ADC2_CHANNEL_7)
  - 分压比: 11.4:1，支持高电压检测
  - 实时电压监控和阈值报警
  - 后台任务持续监控
- **电源芯片数据接收**: GPIO 47 (UART1_RX)
  - 波特率: 9600，8N1配置
  - 数据格式: `[0xFF帧头][电压][电流][CRC]` (4字节)
  - 实时读取电压、电流、功率数据
  - 支持协议分析和调试
- **功能特性**:
  - 电压变化阈值触发
  - UART通信状态监控
  - 数据协议深度分析
  - 完整的调试工具套件
  - 10个专用控制台命令