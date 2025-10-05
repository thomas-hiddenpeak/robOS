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

### 核心文档
- **[项目进度记录](docs/PROJECT_PROGRESS.md)** - 详细的开发进度和完成状态
- **[技术架构文档](docs/TECHNICAL_ARCHITECTURE.md)** - 系统架构设计和技术决策
- **[API参考文档](docs/API_REFERENCE.md)** - 完整的API接口文档
- **[代码规范](docs/CODING_STANDARDS.md)** - 开发规范和最佳实践

### 新功能文档
- **[温度集成指南](docs/TEMPERATURE_INTEGRATION_GUIDE.md)** - AGX温度集成到风扇控制系统的完整指南
- **[智能安全温度策略](docs/SMART_SAFETY_TEMPERATURE_STRATEGY.md)** - 分层安全温度保护机制设计文档
- **[AGX监控更新](docs/AGX_MONITOR_UPDATES.md)** - AGX监控组件开发记录
- **[AGX启动延时功能](docs/AGX_STARTUP_DELAY_FEATURE.md)** - AGX系统启动保护机制

## ⭐ 项目状态

- ✅ **事件管理组件** - 9个测试用例全部通过
- ✅ **硬件抽象层组件** - 5个测试用例全部通过
- ✅ **控制台核心组件** - 8个测试用例全部通过
- ✅ **配置管理组件** - 统一NVS配置管理，支持多种数据类型
- ✅ **风扇控制组件** - 完整PWM控制，温度曲线，配置持久化，智能温度管理
- ✅ **AGX监控组件** - WebSocket实时监控，CPU温度集成，静默运行模式
- ✅ **Matrix LED组件** - 32x32 WS2812矩阵控制，支持像素绘图、动画播放
- ✅ **智能温度管理** - 多层次安全保护，AGX数据集成，调试模式兼容
- 📋 **系统监控组件** - 待开发

## 🏗️ 项目架构

### 组件化架构
```
robOS/
├── components/                    # 独立功能组件
│   ├── console_core/             # 控制台核心组件 🌡️
│   ├── hardware_hal/             # 硬件抽象层组件
│   ├── gpio_controller/          # GPIO通用控制组件 ⚡
│   ├── usb_mux_controller/       # USB MUX切换控制组件 ⚡
│   ├── hardware_commands/        # 硬件控制台命令组件 ⚡
│   ├── config_manager/           # 配置管理组件
│   ├── fan_controller/           # 风扇控制组件 🌡️
│   ├── agx_monitor/              # AGX监控组件 🔍
│   ├── touch_led/                # 触摸LED控制组件 ✨
│   ├── matrix_led/               # 矩阵LED控制组件 ✨
│   ├── board_led/                # 板载LED控制组件 ✨
│   ├── color_correction/         # 色彩校正组件 🎨
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
- **console_core**: 🌡️ UART接口、命令解析器、帮助系统、智能温度管理
- **hardware_hal**: GPIO、PWM、SPI、ADC、UART等底层接口抽象
- **gpio_controller**: ⚡ 通用GPIO控制、安全操作、引脚管理
- **usb_mux_controller**: ⚡ USB-C接口切换、ESP32S3/AGX/N305目标管理
- **hardware_commands**: ⚡ GPIO和USB MUX控制台命令接口
- **config_manager**: 统一NVS配置管理、多数据类型支持、自动提交
- **fan_controller**: 🌡️ PWM风扇控制、温度曲线、多模式运行、配置持久化、智能温度集成
- **agx_monitor**: 🔍 AGX系统监控、WebSocket连接、CPU温度数据推送、静默运行
- **matrix_led**: 32x32 WS2812 LED矩阵控制，像素绘图、动画播放、亮度调节
- **color_correction**: 🎨 WS2812 LED色彩校正，白点/伽马/亮度/饱和度优化，配置管理
- **ethernet_manager**: W5500控制、DHCP服务器、网关功能
- **storage_manager**: TF卡管理、文件系统操作、NVS配置管理
- **power_monitor**: 电压监测、电源芯片通信、功率监控
- **device_manager**: AGX、Orin、N305等设备电源控制和状态监控
- **system_monitor**: ESP32S3系统状态、内存使用、温度监控
- **event_manager**: 事件驱动的组件间通信和状态同步机制

## 板上机柜设备

1. **板载 LED**: GPIO 42，28颗WS2812阵列 - 系统状态指示和装饰照明
2. **触控 LED**: GPIO 45，1颗状态指示WS2812 - 用户交互反馈
3. **矩阵 LED**: GPIO 9，32x32 WS2812矩阵 (1024颗LED) - 显示屏和图形界面
4. **风扇控制**: GPIO 41，25kHz PWM频率，10位分辨率 - 智能温控散热
5. **USB MUX控制**: GPIO 8/48 - 多设备USB接口切换
6. **以太网控制器**: W5500 - 网络通信和管理
7. **TF卡存储**: 配置文件和数据存储
8. **供电电压监测**: 实时电源状态监控
9. **供电芯片**: 电源管理和保护

## 🎨 LED 系统架构

robOS 配备了三个独立的 LED 子系统，每个都有专门的用途和完整的控制接口：

### 1. Touch LED (触摸 LED)
- **硬件**: GPIO 45，单颗 WS2812 LED
- **用途**: 用户交互反馈，系统状态指示
- **特性**: 触摸检测、全彩显示、多种动画模式
- **控制**: `led touch` 命令系列

### 2. Board LED (板载 LED)
- **硬件**: GPIO 42，28颗 WS2812 LED 灯带
- **用途**: 系统装饰照明，环境氛围营造
- **特性**: 独立像素控制、丰富动画效果、亮度调节
- **控制**: `led board` 命令系列

### 3. Matrix LED (矩阵 LED)
- **硬件**: GPIO 9，32x32 WS2812 LED 矩阵 (1024颗)
- **用途**: 图形显示、信息展示、数据可视化
- **特性**: 像素级绘图、图像导入导出、动画播放、SD卡存储
- **控制**: `led matrix` 命令系列

### LED 系统特性
- **全彩支持**: 每个 LED 支持 1677万色彩 (RGB 24位)
- **独立控制**: 三个子系统完全独立，可同时运行不同效果
- **配置持久化**: 所有设置自动保存到 NVS 闪存
- **颜色校正**: 统一的颜色校正系统，支持亮度和饱和度调节
- **文件支持**: Matrix LED 支持图像文件导入导出 (SD卡)
- **线程安全**: 支持多任务环境下的安全操作

## 主要功能

### GPIO通用控制 ⚡
- **支持引脚**: ESP32S3所有可用GPIO引脚（0-21, 26, 33-48）
- **安全操作**: 避免输出状态干扰的设计
- **控制模式**:
  - **输出控制**: 设置GPIO高/低电平输出
  - **输入读取**: 安全的输入模式切换和状态读取
  - **引脚验证**: 自动验证引脚可用性
- **控制台命令**: `gpio <pin> high|low|input`
- **线程安全**: 支持多线程环境下的安全操作

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

**✅ 新增智能温度管理功能**
- 智能温度源优先级系统 - 手动模式、AGX自动模式、默认保护
- AGX CPU温度自动集成 - 实时温度数据推送到风扇控制
- 分层安全保护策略 - 启动保护、离线保护、数据过期保护
- 温度管理命令系统 - `temp` 命令替代 `test temp`，提供完整温度控制

**🔧 技术细节**
- 使用 `temp set <值>` 命令设置手动测试温度（兼容原 `test temp`）
- 使用 `temp auto/manual` 命令在AGX自动模式和手动模式间切换
- 使用 `temp status` 命令查看当前温度源和安全状态
- 曲线模式智能选择温度源：手动 > AGX实时 > 安全默认
- 分层安全温度：启动75°C，离线85°C，过期65°C，备用45°C
- 所有模式都支持线性插值算法，保证平滑的转速变化
- `fan config` 和 `fan help` 命令正确返回成功状态码

### AGX监控功能 🔍
- **连接协议**: WebSocket over Socket.IO
- **服务器地址**: ws://10.10.99.98:58090/socket.io/
- **数据更新频率**: 1Hz实时监控
- **启动保护**: 45秒AGX系统启动延迟
- **静默运行**: 完全静默模式，不干扰控制台操作

#### AGX监控数据
- **CPU信息**: 多核心使用率、频率监控
- **内存信息**: RAM和SWAP使用情况
- **温度监控**: CPU、SoC0-2、Junction温度
- **功耗监控**: GPU+SoC、CPU、系统5V、内存功耗
- **GPU信息**: 3D GPU频率监控

#### AGX监控命令

**基本控制**
```bash
agx_monitor start          # 启动AGX监控
agx_monitor stop           # 停止AGX监控  
agx_monitor status         # 显示连接状态
```

**数据查看**
```bash
agx_monitor data           # 显示完整监控数据
agx_monitor config         # 显示配置信息
agx_monitor stats          # 显示统计信息
agx_monitor debug          # 显示调试信息
```

#### 实时温度集成

**自动数据推送**
- AGX CPU温度自动推送到风扇控制系统
- 1秒更新频率，确保温度响应及时
- 线程安全的数据同步机制

**温度源优先级**（由 `temp` 命令管理）
1. **手动模式** - `temp set 45` 设置测试温度
2. **AGX自动** - 使用实时AGX CPU温度
3. **安全保护** - 多层安全温度策略

**智能安全策略**
```bash
# 系统启动60秒内：75°C高温保护
# AGX离线未连接：85°C紧急保护  
# AGX数据过期>10s：65°C回退保护
# 正常运行：实际AGX CPU温度
```

#### 静默运行特性

**完全静默设计**
- WebSocket连接过程静默
- 数据解析过程静默
- 重连机制静默执行
- 只在关键错误时输出日志

**日志级别控制**
- AGX监控组件：DEBUG级别
- WebSocket库：NONE级别（完全静默）
- 不干扰控制台正常操作

#### 连接管理

**自动重连机制**
- 连接丢失时自动重连
- 3次快速重试（1秒间隔）
- 后续固定间隔重连（3秒）
- 45秒AGX启动保护延迟

**连接状态监控**
```bash
agx_monitor status
# 输出：
# Connection Status: Connected
# Server: 10.10.99.98:58090
# Uptime: 5 minutes 23 seconds
# Data Updates: 323 received
```

#### 配置管理

**默认配置**
```c
server_url = "10.10.99.98";
server_port = 58090;
reconnect_interval = 3000ms;
startup_delay = 45000ms;  // AGX启动保护
heartbeat_timeout = 10000ms;
```

**配置持久化**
- 所有配置自动保存到NVS
- 支持运行时配置修改
- 重启后自动恢复设置

#### 开发状态

**✅ 已完成功能**
- WebSocket连接和数据接收
- 完整数据解析（CPU、内存、温度、功耗、GPU）
- 静默运行模式
- 自动重连机制
- 温度数据自动推送到风扇控制
- 控制台命令系统

**🔧 技术特性**
- 事件驱动架构
- 线程安全数据访问
- 内存优化设计
- 错误处理和恢复
- 与风扇控制系统深度集成

### 智能温度管理系统 🌡️

#### 温度命令系统
```bash
temp set 45        # 设置手动测试温度
temp get           # 获取当前有效温度  
temp auto          # 切换到AGX自动模式
temp manual        # 切换到手动测试模式
temp status        # 显示详细温度状态
```

#### 温度源优先级策略

**1. 手动模式**（最高优先级）
- 触发：`temp set <温度>` 命令
- 用途：调试和测试风扇曲线
- 兼容：支持原有 `test temp` 命令

**2. AGX自动模式**（智能分层保护）
- **启动保护**：系统启动60秒内使用75°C
- **离线保护**：AGX从未连接时使用85°C  
- **过期保护**：AGX数据>10秒时使用65°C
- **正常运行**：使用实时AGX CPU温度

**3. 系统备用**（最低优先级）
- 互斥锁超时等异常情况使用45°C

#### 安全保护机制

**时间戳追踪**
- 系统启动时间记录
- AGX数据更新时间戳
- 智能状态判断和切换

**状态监控显示**
```bash
temp status
# 输出示例：
# Temperature Mode: AGX Auto
# Effective Temperature: 75.0°C
# Temperature Source: Startup Protection (High temp for 60s startup safety)
# System Uptime: 45 seconds
# AGX Data: Never received
```

### USB MUX控制
- **MUX1引脚**: GPIO 8 - USB MUX1选择控制
- **MUX2引脚**: GPIO 48 - USB MUX2选择控制
- 支持切换USB-C接口连接目标：
  - **ESP32S3**: mux1=0, mux2=0 (默认)
  - **AGX**: mux1=1, mux2=0
  - **N305**: mux1=1, mux2=1
- 控制台命令: `usbmux esp32s3|agx|n305|status`

### 触摸LED控制功能 ✨
- **LED数据引脚**: GPIO 8 - WS2812 LED灯条数据线
- **触摸传感器引脚**: GPIO 4 - 电容式触摸传感器
- **LED规格**: 支持WS2812/WS2812B可编程LED灯条，最多256个LED
- **工作模式**: 
  - **触摸交互**: 触摸时LED变绿，释放后恢复蓝色
  - **长按模式**: 长按1秒以上启动彩虹动画
  - **双击模式**: 双击切换亮度等级
  - **呼吸动画**: 默认蓝色呼吸效果

#### 触摸LED工作模式详细说明

**1. 触摸响应模式**
- 默认状态：柔和蓝色呼吸效果 (亮度50)
- 触摸时：立即切换为绿色 (亮度150)
- 释放时：恢复蓝色并重启呼吸动画
- 响应延迟：<50ms，实时触摸反馈

**2. 长按功能**
- 长按检测：触摸时间 >1000ms
- 长按效果：启动彩虹循环动画
- 动画速度：中等速度 (100/255)
- 持续时间：直到下次触摸或系统重置

**3. 双击亮度切换**
- 检测逻辑：两次快速触摸 (间隔<500ms)
- 亮度级别：低亮度30 ↔ 高亮度200
- 智能切换：基于当前亮度自动选择目标亮度

**4. 动画效果系统**
- **呼吸动画**: 正弦波亮度变化，营造自然呼吸感
- **彩虹动画**: HSV色彩空间循环，每个LED不同相位
- **渐变动画**: 颜色平滑过渡
- **脉冲效果**: 快速闪烁效果

#### 硬件配置

**LED灯条配置**
```c
led_gpio = GPIO_NUM_8;         // WS2812数据线
led_count = 16;                // LED数量
max_brightness = 200;          // 最大亮度
led_pixel_format = GRB;        // 像素格式
resolution_hz = 10MHz;         // RMT时钟频率
```

**触摸传感器配置**
```c  
touch_gpio = GPIO_NUM_4;       // 触摸传感器引脚
touch_threshold = 1000;        // 触摸检测阈值
touch_invert = false;          // 触摸逻辑 (false=低电平有效)
sampling_rate = 20Hz;          // 采样频率
```

#### 使用场景示例

**场景1：系统状态指示**
- 启动时：柔和蓝色呼吸，表示系统正常运行
- 错误时：红色闪烁，表示系统异常
- 更新时：紫色脉冲，表示固件更新中

**场景2：用户交互反馈**  
- 触摸操作：绿色确认反馈
- 长按功能：彩虹动画娱乐模式
- 双击调节：智能亮度适应环境

**场景3：环境适应**
- 白天模式：高亮度(200)，清晰可见
- 夜间模式：低亮度(30)，温和不刺眼
- 自动调节：双击快速切换亮度级别

#### 技术特性

**🎨 色彩支持**
- 全彩RGB控制，1677万色彩
- 预定义色彩常量：红、绿、蓝、白、黄、青、品红、橙、紫
- HSV色彩空间支持，方便渐变和动画

**⚡ 性能优化**
- 硬件PWM驱动，CPU占用率低
- 多任务安全，互斥锁保护
- 内存优化：仅2KB基础占用 + LED缓冲区

**🔧 开发友好**
- 丰富的API接口，支持单个和批量LED控制
- 事件回调机制，支持自定义触摸响应
- 线程安全设计，支持多任务调用

**⚠️ 当前限制**
- 硬件依赖：需要外接WS2812灯条和触摸传感器
- 引脚冲突：GPIO 8可能与其他功能冲突，需要硬件配置协调
- 触摸精度：受环境湿度和电磁干扰影响

#### 触摸LED控制台命令

**基本状态查看**
```bash
led touch status                  # 显示LED状态和配置信息
led touch help                    # 显示完整命令参考
```

**LED颜色控制**
```bash
led touch set <color>             # 设置LED颜色(单个LED)
led touch set red                 # LED设为红色
led touch set FF6600              # 使用RGB十六进制(橙色)
led touch brightness <level>      # 设置亮度(0-255)
led touch clear                   # 关闭LED
```

**动画控制**
```bash
led touch animation start <mode> [speed] [color1] [color2]
led touch animation start rainbow 150   # 快速彩虹动画
led touch animation start breathe 50 green  # 缓慢绿色呼吸
led touch animation stop          # 停止动画
```

**触摸传感器控制**
```bash
led touch sensor enable           # 启用触摸检测
led touch sensor disable          # 禁用触摸检测
led touch sensor threshold <value> # 设置触摸阈值(0-4095)
```

**使用场景示例**

*场景1：系统状态指示*
```bash
led touch set blue                # 系统正常-蓝色
led touch animation start breathe 30 blue  # 待机呼吸效果
led touch set red                 # 系统异常-红色闪烁
```

*场景2：用户交互测试*
```bash
led touch sensor threshold 800    # 设置中等灵敏度
led touch set green               # 设置确认颜色
led touch animation start rainbow 100   # 娱乐模式
```

*场景3：亮度适应*
```bash
led touch brightness 200          # 白天模式-高亮度
led touch brightness 30           # 夜间模式-低亮度
led touch set white               # 工作照明
```

### Matrix LED控制功能 ✨
- **LED矩阵引脚**: GPIO 9 - 32x32 WS2812矩阵数据线
- **矩阵规格**: 32x32像素，共1024颗可编程RGB LED
- **驱动方式**: RMT硬件驱动，支持DMA加速
- **颜色深度**: 24位真彩色，支持1600万种颜色
- **显示功能**:
  - **像素级控制**: 单个像素精确设置和读取
  - **几何绘图**: 直线、矩形、圆形等基本图形
  - **图案显示**: 测试图案、自定义图像
  - **动画播放**: 彩虹、波浪、呼吸、旋转等动态效果

#### Matrix LED工作模式详细说明

**1. 静态显示模式 (static)**
- 显示固定的图像或图案
- 支持像素级精确控制
- 适合状态指示、图标显示
- 内存占用低，性能稳定

**2. 动画播放模式 (animation)**
- 支持多种预置动画效果
- 可配置动画速度、颜色、循环次数
- 实时动画渲染，流畅播放
- 支持动画的播放、暂停、停止控制

**3. 自定义模式 (custom)**
- 支持加载外部动画文件
- 灵活的动画编程接口
- 适合复杂的显示需求
- 可扩展的动画系统

**4. 关闭模式 (off)**
- 完全关闭LED矩阵显示
- 节能模式，降低功耗
- 可用于夜间或待机状态

#### Matrix LED控制命令详解

**基本状态和控制**
```bash
led matrix status                    # 显示矩阵状态信息
led matrix enable on|off             # 启用/禁用矩阵显示
led matrix brightness 50             # 设置亮度(0-100%)
```

**像素和图形操作**
```bash
led matrix clear                     # 清空所有像素
led matrix fill 255 0 0              # 填充红色(RGB格式)
led matrix pixel 10 15 0 255 0       # 设置像素(10,15)为绿色
led matrix test                      # 显示测试图案
```

**几何图形绘制**
```bash
led matrix draw line 0 0 31 31 255 255 255      # 绘制白色对角线
led matrix draw rect 5 5 10 8 255 0 255 fill    # 绘制填充紫色矩形
led matrix draw circle 16 16 8 0 255 255 fill   # 绘制填充青色圆形
```

**显示模式控制**
```bash
led matrix mode static               # 切换到静态显示模式
led matrix mode animation            # 切换到动画播放模式
led matrix mode off                  # 关闭矩阵显示
```

**动画播放控制**
```bash
led matrix animation rainbow 70      # 播放彩虹动画，速度70%
led matrix animation wave 50         # 播放波浪动画，速度50%
led matrix animation breathe 60      # 播放呼吸动画，速度60%
led matrix animation rotate 40       # 播放旋转动画，速度40%
led matrix animation fade 30         # 播放渐变动画，速度30%
led matrix stop                      # 停止当前动画
```

**配置管理**
```bash
led matrix config save              # 保存当前配置到NVS
led matrix config load              # 从NVS加载配置
led matrix config reset             # 重置为默认配置
```

#### 配置显示格式

**状态概览** (`led matrix status`)：
```
Matrix LED Status:
  Initialized: Yes
  Enabled: Yes
  Mode: 1 (Animation)
  Brightness: 75%
  Pixel Count: 1024
  Frame Count: 1520
  Current Animation: rainbow_70
```

#### 使用场景示例

**场景1：系统状态指示**
```bash
led matrix clear                     # 清空显示
led matrix fill 0 255 0              # 绿色表示正常状态
led matrix brightness 30             # 设置适中亮度
```

**场景2：信息可视化**
```bash
led matrix clear
led matrix draw rect 0 0 32 8 255 0 0 fill      # 红色区域表示CPU使用率
led matrix draw rect 0 8 24 8 255 255 0 fill    # 黄色区域表示内存使用率
led matrix draw rect 0 16 16 8 0 0 255 fill     # 蓝色区域表示网络状态
```

**场景3：装饰效果**
```bash
led matrix animation rainbow 60      # 彩虹循环效果
# 或
led matrix animation breathe 40      # 柔和的呼吸灯效果
```

**场景4：测试和调试**
```bash
led matrix test                      # 显示测试图案验证硬件
led matrix pixel 0 0 255 0 0         # 测试左上角像素
led matrix pixel 31 31 0 0 255       # 测试右下角像素
```

#### 技术特性

**🎨 丰富的显示功能**
- 1024个独立控制的RGB LED像素
- 101级亮度调节(0-100%)
- 预定义颜色常量和RGB/HSV颜色空间转换
- 颜色插值和亮度应用算法

**⚡ 高性能渲染**
- RMT硬件驱动，CPU占用率低
- DMA传输，支持高帧率显示
- 双缓冲机制，避免显示撕裂
- 优化的几何图形绘制算法

**🔧 灵活的控制**
- 多种显示模式和动画效果
- 实时动画参数调节
- 配置持久化存储
- 完整的控制台命令接口

**🛡️ 可靠性保证**
- 线程安全的并发访问
- 完整的错误处理机制
- 硬件状态监控和恢复
- 全面的单元测试覆盖

#### 开发状态说明

**✅ 已完成功能**
- 基础像素控制 - 完全实现
- 几何图形绘制 - 完全实现
- 动画播放系统 - 完全实现
- 亮度和模式控制 - 完全实现
- 配置持久化 - 完全实现
- 控制台命令接口 - 完全实现
- 颜色工具函数 - 完全实现

**🚧 开发中功能**
- 文本渲染 - 基础框架已实现，字体库待完善
- 自定义动画加载 - 接口已定义，文件解析待实现
- 高级特效 - 基础效果已实现，更多效果开发中

**🔧 技术细节**
- 使用Bresenham算法进行直线绘制
- 采用中点圆算法进行圆形绘制
- 支持HSV和RGB颜色空间的双向转换
- 实现了平滑的颜色插值算法
- 配置数据通过NVS进行持久化存储


### 网络管理功能 (W5500以太网) 🌐
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
  - 网关: 10.10.99.100（某些文档中为10.10.99.1）
  - DNS: 8.8.8.8
- **DHCP服务器**:
  - IP池范围: 10.10.99.100 - 10.10.99.101（可扩展到110）
  - 默认租期: 24小时
  - 最大客户端数: 10
  - 支持客户端跟踪和管理
- **功能特性**:
  - 静态IP配置和动态DHCP
  - 内置DHCP服务器功能
  - 网络活动日志记录
  - 网络状态统计和监控
  - 网络接口控制和重置
  - NVS配置持久化
  - 网络调试和诊断工具
  - 完整的控制台命令接口
- 参考项目
https://github.com/thomas-hiddenpeak/rm01-esp32s3-bsp

#### 网络管理命令详解

**基础状态和控制**
```bash
net                       # 显示所有网络管理命令帮助
net status                # 显示网络接口状态（链路、IP配置、统计信息）
net start                 # 启动网络接口
net stop                  # 停止网络接口  
net reset                 # 重置网络接口
```

**网络配置管理**
```bash
# 配置查看和管理
net config                # 显示当前网络配置
net config show           # 显示当前网络配置（同上）
net config save           # 保存配置到NVS存储
net config load           # 从NVS存储加载配置
net config reset          # 重置为默认配置

# 网络参数设置
net config set <参数> <值>  # 设置网络参数
```

**可配置的网络参数**
```bash
net config set ip <x.x.x.x>              # 设置静态IP地址
net config set netmask <x.x.x.x>         # 设置网络掩码
net config set gateway <x.x.x.x>         # 设置网关地址
net config set dns <x.x.x.x>             # 设置DNS服务器
net config set dhcp_pool_start <x.x.x.x> # 设置DHCP池起始IP
net config set dhcp_pool_end <x.x.x.x>   # 设置DHCP池结束IP
net config set dhcp_lease_hours <n>      # 设置DHCP租期（小时，1-8760）
net config set dhcp_max_clients <n>      # 设置最大DHCP客户端数（1-50）
```

**配置示例**
```bash
net config set ip 10.10.99.97            # 设置IP地址
net config set dns 8.8.8.8               # 设置DNS服务器
net config set dhcp_lease_hours 24       # 设置24小时租期
net config save                          # 保存配置
net reset                                # 应用配置
```

**DHCP服务器管理**
```bash
net dhcp                  # 显示DHCP服务器状态
net dhcp enable           # 启用DHCP服务器
net dhcp disable          # 禁用DHCP服务器
```

**网络活动日志**
```bash
# 基础日志查看
net log                   # 显示16条最近的网络活动日志
net log all               # 显示所有存储的日志条目（最多32条）
net log verbose           # 显示详细信息
net log count=N           # 显示N条最近的日志（1-32）
net log help              # 显示日志命令帮助

# 日志选项组合示例
net log verbose all       # 显示所有日志的详细信息
net log count=5           # 显示5条最近的日志
```

**网络调试工具**
```bash
# DHCP调试命令
net debug                 # 显示DHCP调试命令帮助
net debug reset          # 重置DHCP调试计数器
net debug timing         # 显示DHCP时序分析
net debug force-restart  # 强制重启DHCP客户端
net debug status         # 显示调试状态
```

**网络状态信息说明**

`net status` 显示的信息包括：
- **网络接口状态**：初始化状态、启动状态、链路状态
- **MAC地址**：硬件MAC地址
- **IP配置**：IP地址、网关、子网掩码、DNS服务器
- **DHCP服务器配置**：启用状态、IP池范围、租期、最大客户端数
- **网络统计**：收发包数、字节数、错误数

`net log` 显示的信息包括：
- 网络接口启动/停止事件
- 链路状态变化
- DHCP事件和IP地址分配
- 网络错误和警告

**使用场景示例**

*场景1：基本网络设置*
```bash
net status                         # 检查当前状态
net config set ip 10.10.99.98      # 设置新IP
net config set gateway 10.10.99.1  # 设置网关
net config save                    # 保存配置
net reset                          # 应用配置
```

*场景2：DHCP服务器配置*
```bash
net config set dhcp_pool_start 10.10.99.100    # 设置DHCP池起始
net config set dhcp_pool_end 10.10.99.110      # 设置DHCP池结束  
net config set dhcp_lease_hours 48             # 设置48小时租期
net dhcp enable                                # 启用DHCP服务器
net config save                                # 保存配置
```

*场景3：网络故障排除*
```bash
net status                    # 检查接口状态
net log                       # 查看最近活动
net debug status              # 检查调试信息
net reset                     # 尝试重置接口
system reboot                 # 如需要可重启系统
```

*场景4：网络监控和维护*
```bash
net log verbose all           # 查看详细网络活动历史
net debug timing              # 分析网络性能
net status                    # 实时状态监控
```

#### 开发状态说明

**✅ 已完成功能**
- 完整的网络配置管理系统
- DHCP服务器和客户端支持
- 网络状态监控和统计
- 活动日志记录和查看
- 配置持久化存储
- 完整的控制台命令接口
- 网络调试和诊断工具

**🔧 技术特性**
- W5500硬件以太网控制器支持
- SPI通信接口优化
- 线程安全的网络操作
- 事件驱动的状态管理
- 与系统其他组件的深度集成


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

## 🚀 快速开始

### 1. 系统状态检查
```bash
help                    # 查看系统概览和可用命令
status                  # 显示控制台状态
temp status             # 检查温度管理状态
```

### 2. AGX监控启动
```bash
agx_monitor start       # 启动AGX系统监控
agx_monitor status      # 检查连接状态
agx_monitor data        # 查看监控数据
```

### 3. 智能风扇控制
```bash
fan status              # 查看风扇状态
temp auto               # 启用AGX温度自动控制
fan mode 0 curve        # 设置风扇为曲线模式
fan config curve 0 40:20 50:40 60:70 70:100  # 配置温度曲线
```

### 4. 手动调试模式
```bash
temp manual             # 切换到手动模式
temp set 50             # 设置测试温度50°C
fan status              # 查看风扇响应
temp auto               # 切换回自动模式
```

### 5. LED 系统快速体验
```bash
# 触摸 LED - 用户交互指示
led touch set blue                    # 设置触摸LED为蓝色
led touch animation start rainbow 100 # 启动彩虹动画

# 板载 LED - 系统装饰照明
led board all 255 100 0              # 设置所有板载LED为橙色
led board anim breathe 0 255 0 50     # 启动绿色呼吸动画

# 矩阵 LED - 图形显示
led matrix fill 255 0 255            # 矩阵填充紫色
led matrix draw circle 16 16 8 255 255 0 fill  # 画一个黄色实心圆
led matrix anim wave 60               # 启动波浪动画
```

### 6. LED 配置保存
```bash
led touch config save               # 保存触摸LED配置
led board config save               # 保存板载LED配置  
led matrix config save              # 保存矩阵LED配置
```

### 7. 网络管理快速配置
```bash
# 检查网络状态
net status                          # 显示当前网络状态
net log                             # 查看网络活动日志

# 基础网络配置
net config set ip 10.10.99.98       # 设置新IP地址
net config set gateway 10.10.99.1   # 设置网关
net config set dns 8.8.8.8          # 设置DNS服务器
net config save                     # 保存配置

# DHCP服务器配置
net config set dhcp_pool_start 10.10.99.100  # 设置DHCP池起始
net config set dhcp_pool_end 10.10.99.110    # 设置DHCP池结束
net dhcp enable                              # 启用DHCP服务器

# 应用配置
net reset                           # 重置网络接口应用配置
net status                          # 验证配置是否生效
```

### 8. 色彩校正快速优化
```bash
# 查看当前色彩校正状态
color status                        # 显示所有校正参数

# 标准显示优化 (推荐新手)
color enable                        # 启用色彩校正
color gamma 2.2                     # 设置标准伽马
color saturation 1.1                # 轻微增强饱和度

# 环境适应性调节
color whitepoint 0.9 1.0 1.1        # 冷色调环境
color brightness 1.2                # 提高整体亮度

# 一键重置和备份
color export /sdcard/my_color.json  # 备份当前配置
color reset                         # 重置为默认值
```

## 📋 命令参考

### 🌡️ 温度管理命令
| 命令 | 说明 | 示例 |
|------|------|------|
| `temp status` | 显示温度管理状态 | `temp status` |
| `temp get` | 获取当前有效温度 | `temp get` |
| `temp set <value>` | 设置手动测试温度 | `temp set 45` |
| `temp auto` | 切换到AGX自动模式 | `temp auto` |
| `temp manual` | 切换到手动测试模式 | `temp manual` |

### 🔍 AGX监控命令
| 命令 | 说明 | 示例 |
|------|------|------|
| `agx_monitor start` | 启动AGX监控 | `agx_monitor start` |
| `agx_monitor stop` | 停止AGX监控 | `agx_monitor stop` |
| `agx_monitor status` | 显示连接状态 | `agx_monitor status` |
| `agx_monitor data` | 显示监控数据 | `agx_monitor data` |
| `agx_monitor config` | 显示配置信息 | `agx_monitor config` |
| `agx_monitor stats` | 显示统计信息 | `agx_monitor stats` |

### 🌪️ 风扇控制命令
| 命令 | 说明 | 示例 |
|------|------|------|
| `fan status` | 显示风扇状态 | `fan status` |
| `fan set <id> <speed>` | 设置风扇转速 | `fan set 0 75` |
| `fan mode <id> <mode>` | 设置工作模式 | `fan mode 0 curve` |
| `fan enable <id> <on/off>` | 启用/禁用风扇 | `fan enable 0 on` |
| `fan config curve <id> <points>` | 配置温度曲线 | `fan config curve 0 30:20 50:40` |
| `fan config save` | 保存配置 | `fan config save` |

### ⚡ GPIO控制命令
| 命令 | 说明 | 示例 |
|------|------|------|
| `gpio <pin> high` | 设置GPIO高电平 | `gpio 42 high` |
| `gpio <pin> low` | 设置GPIO低电平 | `gpio 42 low` |
| `gpio <pin> input` | 设置为输入模式 | `gpio 42 input` |
| `usbmux <target>` | 切换USB MUX | `usbmux agx` |

### 🎨 LED 控制命令

robOS 系统包含三个 LED 子系统，每个都有独立的控制命令：

#### 🔸 Touch LED 命令 (单个 WS2812 触摸 LED)
| 命令 | 说明 | 示例 |
|------|------|------|
| `led touch status` | 显示 LED 状态和配置 | `led touch status` |
| `led touch help` | 显示完整命令参考 | `led touch help` |
| `led touch set <color>` | 设置颜色 | `led touch set red` |
| `led touch set <RRGGBB>` | 使用 RGB 十六进制设置 | `led touch set FF6600` |
| `led touch brightness <0-255>` | 设置亮度 | `led touch brightness 128` |
| `led touch clear` | 关闭 LED | `led touch clear` |
| `led touch animation start <type> [speed] [color]` | 启动动画 | `led touch animation start rainbow 100` |
| `led touch animation stop` | 停止动画 | `led touch animation stop` |
| `led touch sensor threshold <value>` | 设置触摸阈值 | `led touch sensor threshold 500` |
| `led touch sensor enable/disable` | 启用/禁用触摸检测 | `led touch sensor enable` |
| `led touch config save/load/reset` | 配置管理 | `led touch config save` |

**动画类型**: `rainbow`, `breathe`, `fade`, `pulse`, `sparkle`

#### 🔸 Board LED 命令 (28颗 WS2812 板载 LED)
| 命令 | 说明 | 示例 |
|------|------|------|
| `led board help` | 显示帮助信息 | `led board help` |
| `led board on` | 打开所有 LED（白色） | `led board on` |
| `led board off` | 关闭所有 LED | `led board off` |
| `led board clear` | 清除所有 LED | `led board clear` |
| `led board all <R> <G> <B>` | 设置所有 LED 颜色 | `led board all 255 0 0` |
| `led board set <index> <R> <G> <B>` | 设置特定 LED 颜色 | `led board set 5 0 255 0` |
| `led board brightness <0-255>` | 设置亮度 | `led board brightness 100` |
| `led board anim stop` | 停止动画 | `led board anim stop` |
| `led board anim fade <R> <G> <B> [speed]` | 淡入淡出动画 | `led board anim fade 255 0 0 50` |
| `led board anim rainbow [speed]` | 彩虹动画 | `led board anim rainbow 80` |
| `led board anim breathe <R> <G> <B> [speed]` | 呼吸动画 | `led board anim breathe 0 0 255 30` |
| `led board anim wave <R> <G> <B> [speed]` | 波浪动画 | `led board anim wave 255 255 0 60` |
| `led board anim chase <R> <G> <B> [speed]` | 追逐动画 | `led board anim chase 255 0 255 70` |
| `led board anim twinkle <R> <G> <B> [speed]` | 闪烁动画 | `led board anim twinkle 255 255 255 40` |
| `led board anim fire [speed]` | 火焰动画 | `led board anim fire 50` |
| `led board anim pulse <R> <G> <B> [speed]` | 脉冲动画 | `led board anim pulse 0 255 0 80` |
| `led board anim gradient <R1> <G1> <B1> <R2> <G2> <B2> [speed]` | 渐变动画 | `led board anim gradient 255 0 0 0 0 255 60` |
| `led board config save/load/reset` | 配置管理 | `led board config save` |

#### 🔸 Matrix LED 命令 (32x32 WS2812 LED 矩阵)
| 命令 | 说明 | 示例 |
|------|------|------|
| `led matrix status` | 显示矩阵状态 | `led matrix status` |
| `led matrix help` | 显示完整帮助 | `led matrix help` |
| `led matrix enable <on/off>` | 启用/禁用矩阵 | `led matrix enable on` |
| `led matrix brightness <0-100>` | 设置亮度百分比 | `led matrix brightness 80` |
| `led matrix clear` | 清除所有像素 | `led matrix clear` |
| `led matrix fill <r> <g> <b>` | 用颜色填充 | `led matrix fill 255 0 0` |
| `led matrix pixel <x> <y> <r> <g> <b>` | 设置单个像素 | `led matrix pixel 16 16 0 255 0` |
| `led matrix test` | 显示测试图案 | `led matrix test` |
| `led matrix draw line <x0> <y0> <x1> <y1> <r> <g> <b>` | 画线 | `led matrix draw line 0 0 31 31 255 255 0` |
| `led matrix draw rect <x> <y> <w> <h> <r> <g> <b> [fill]` | 画矩形 | `led matrix draw rect 10 10 12 8 0 255 255 fill` |
| `led matrix draw circle <x> <y> <radius> <r> <g> <b> [fill]` | 画圆 | `led matrix draw circle 16 16 8 255 0 255` |
| `led matrix anim <type> [speed]` | 启动动画 | `led matrix anim rainbow 60` |
| `led matrix stop` | 停止动画 | `led matrix stop` |
| `led matrix mode <static/animation/off>` | 设置显示模式 | `led matrix mode animation` |
| `led matrix config save/load/export/import` | 配置管理 | `led matrix config save` |
| `led matrix image export <file>` | 导出当前显示 | `led matrix image export /sdcard/image.json` |
| `led matrix image import <file> [name]` | 导入图像文件 | `led matrix image import /sdcard/image.json logo` |
| `led matrix storage status` | 检查 SD 卡状态 | `led matrix storage status` |

**矩阵动画类型**: `rainbow`, `wave`, `breathe`, `rotate`, `fade`

#### 🔸 颜色校正命令

robOS 提供了功能强大的色彩校正系统，专门为 WS2812 LED 设备设计，支持白点校正、伽马校正、亮度增强和饱和度增强等多种色彩处理功能。

| 命令 | 说明 | 示例 |
|------|------|------|
| `color enable/disable` | 启用/禁用色彩校正系统 | `color enable` |
| `color status` | 显示当前配置状态 | `color status` |
| `color whitepoint <r> <g> <b>` | RGB通道白点校正 (0.0-2.0) | `color whitepoint 0.9 1.0 1.1` |
| `color gamma <value>` | 伽马校正 (0.1-4.0) | `color gamma 2.2` |
| `color brightness <factor>` | 亮度增强 (0.0-2.0) | `color brightness 1.2` |
| `color saturation <factor>` | 饱和度增强 (0.0-2.0) | `color saturation 1.1` |
| `color export/import <file>` | 配置文件导入导出 | `color export /sdcard/color.json` |
| `color reset` | 重置为默认设置 | `color reset` |

**快速配置场景**:
- **标准优化**: `color enable && color gamma 2.2 && color saturation 1.1`
- **暖色调**: `color whitepoint 1.1 1.0 0.9 && color brightness 0.9`
- **冷色调**: `color whitepoint 0.9 1.0 1.1 && color brightness 1.1`
- **护眼模式**: `color whitepoint 1.0 0.95 0.85 && color brightness 0.7`

�� **详细指南**: [色彩校正系统完整指南](docs/COLOR_CORRECTION_GUIDE.md)

### 🌐 网络管理命令
| 命令 | 说明 | 示例 |
|------|------|------|
| `net status` | 显示网络接口状态 | `net status` |
| `net start` | 启动网络接口 | `net start` |
| `net stop` | 停止网络接口 | `net stop` |
| `net reset` | 重置网络接口 | `net reset` |
| `net config show` | 显示当前网络配置 | `net config show` |
| `net config set <param> <value>` | 设置网络参数 | `net config set ip 10.10.99.97` |
| `net config save` | 保存配置到NVS | `net config save` |
| `net config load` | 从NVS加载配置 | `net config load` |
| `net config reset` | 重置为默认配置 | `net config reset` |
| `net dhcp` | 显示DHCP服务器状态 | `net dhcp` |
| `net dhcp enable` | 启用DHCP服务器 | `net dhcp enable` |
| `net dhcp disable` | 禁用DHCP服务器 | `net dhcp disable` |
| `net log` | 显示网络活动日志 | `net log` |
| `net log all` | 显示所有日志条目 | `net log all` |
| `net log verbose` | 显示详细日志信息 | `net log verbose` |
| `net log count=N` | 显示N条最近日志 | `net log count=5` |
| `net debug status` | 显示调试状态 | `net debug status` |
| `net debug timing` | 显示时序分析 | `net debug timing` |
| `net debug reset` | 重置调试计数器 | `net debug reset` |
| `net debug force-restart` | 强制重启DHCP客户端 | `net debug force-restart` |

**可配置的网络参数**:
- `ip` - 静态IP地址 (x.x.x.x 格式)
- `netmask` - 网络掩码 (x.x.x.x 格式)  
- `gateway` - 网关地址 (x.x.x.x 格式)
- `dns` - DNS服务器 (x.x.x.x 格式)
- `dhcp_pool_start` - DHCP池起始IP (x.x.x.x 格式)
- `dhcp_pool_end` - DHCP池结束IP (x.x.x.x 格式)
- `dhcp_lease_hours` - DHCP租期小时数 (1-8760)
- `dhcp_max_clients` - 最大DHCP客户端数 (1-50)

### 🔧 系统命令
| 命令 | 说明 | 示例 |
|------|------|------|
| `help` | 显示系统概览和命令列表 | `help` |
| `help <command>` | 显示特定命令帮助 | `help temp` |
| `version` | 显示版本信息 | `version` |
| `status` | 显示控制台状态 | `status` |
| `clear` | 清屏 | `clear` |
| `history` | 显示命令历史 | `history` |

## 📚 相关文档

### 功能使用指南
- **[温度集成指南](docs/TEMPERATURE_INTEGRATION_GUIDE.md)** - AGX温度集成到风扇控制的完整使用指南
- **[智能安全温度策略](docs/SMART_SAFETY_TEMPERATURE_STRATEGY.md)** - 分层安全温度保护机制详解
- **[色彩校正系统完整指南](docs/COLOR_CORRECTION_GUIDE.md)** - WS2812 LED色彩校正详细配置和使用指南

### LED 系统文档
- **[Touch LED 组件](components/touch_led/README.md)** - 触摸LED详细使用指南和API文档
- **[Matrix LED 组件](components/matrix_led/README.md)** - 32x32 LED矩阵控制和图形编程文档
- **[Board LED 组件](components/board_led/)** - 板载LED灯带控制和动画效果文档
- **[色彩校正组件](components/color_correction/)** - WS2812 LED色彩校正和优化系统文档

### 开发技术文档
- **[项目进度记录](docs/PROJECT_PROGRESS.md)** - 详细的开发进度和完成状态
- **[技术架构文档](docs/TECHNICAL_ARCHITECTURE.md)** - 系统架构设计和技术决策
- **[API参考文档](docs/API_REFERENCE.md)** - 完整的API接口文档
- **[代码规范](docs/CODING_STANDARDS.md)** - 开发规范和最佳实践

### 特性开发记录
- **[AGX监控更新](docs/AGX_MONITOR_UPDATES.md)** - AGX监控组件开发记录
- **[AGX启动延迟功能](docs/AGX_STARTUP_DELAY_FEATURE.md)** - AGX系统启动保护机制
- **[绝对静默模式](docs/ABSOLUTE_SILENCE_FINAL.md)** - AGX监控静默运行实现

---

**项目版本**: robOS v2.0.0  
**更新日期**: 2025年10月5日  
**开发团队**: robOS Team