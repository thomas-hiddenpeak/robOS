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
- ✅ **Matrix LED组件** - 32x32 WS2812矩阵控制，支持像素绘图、动画播放
- 📋 **系统监控组件** - 待开发

## 🏗️ 项目架构

### 组件化架构
```
robOS/
├── components/                    # 独立功能组件
│   ├── console_core/             # 控制台核心组件
│   ├── hardware_hal/             # 硬件抽象层组件
│   ├── gpio_controller/          # GPIO通用控制组件 ⚡
│   ├── usb_mux_controller/       # USB MUX切换控制组件 ⚡
│   ├── hardware_commands/        # 硬件控制台命令组件 ⚡
│   ├── config_manager/           # 配置管理组件
│   ├── fan_controller/           # 风扇控制组件
│   ├── touch_led/                # 触摸LED控制组件 ✨
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
- **gpio_controller**: ⚡ 通用GPIO控制、安全操作、引脚管理
- **usb_mux_controller**: ⚡ USB-C接口切换、ESP32S3/AGX/N305目标管理
- **hardware_commands**: ⚡ GPIO和USB MUX控制台命令接口
- **config_manager**: 统一NVS配置管理、多数据类型支持、自动提交
- **fan_controller**: PWM风扇控制、温度曲线、多模式运行、配置持久化
- **matrix_led**: 32x32 WS2812 LED矩阵控制，像素绘图、动画播放、亮度调节
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
  - IP池范围: 10.10.99.100 - 10.10.99.101
  - 默认租期: 24小时
  - 支持客户端跟踪和管理
- **功能特性**:
  - 静态IP配置
  - DHCP服务器功能
  - NVS配置持久化
- 参考项目
https://github.com/thomas-hiddenpeak/rm01-esp32s3-bsp


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