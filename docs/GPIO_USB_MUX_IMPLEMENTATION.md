# GPIO控制和USB MUX功能文档

## 概述

本次更新为robOS添加了两个重要的硬件控制功能：
1. **GPIO通用控制** - 提供安全的GPIO操作，支持输出控制和输入读取
2. **USB MUX控制** - 支持USB-C接口在ESP32S3、AGX、N305之间切换

## 新增组件

### 1. gpio_controller 组件
位置：`components/gpio_controller/`

**功能特性：**
- 安全的GPIO输出控制（高/低电平）
- 输入模式读取，自动切换模式并读取
- 避免输出状态干扰的设计
- 线程安全操作
- 引脚配置管理和验证

**主要API：**
- `gpio_controller_init()` - 初始化GPIO控制器
- `gpio_controller_set_output(pin, state)` - 设置GPIO输出
- `gpio_controller_read_input(pin, *state)` - 安全读取GPIO输入
- `gpio_controller_validate_pin(pin)` - 验证GPIO引脚

### 2. usb_mux_controller 组件
位置：`components/usb_mux_controller/`

**功能特性：**
- USB-C接口切换控制（ESP32S3/AGX/N305）
- 基于GPIO控制器的底层实现
- 状态管理和持久化
- 安全的切换操作
- 线程安全保护

**硬件配置：**
- MUX1引脚：GPIO 8 - USB MUX1选择控制
- MUX2引脚：GPIO 48 - USB MUX2选择控制

**控制逻辑：**
| 目标设备 | MUX1 | MUX2 | 描述 |
|---------|------|------|------|
| ESP32S3 | 0    | 0    | 连接到ESP32S3（默认） |
| AGX     | 1    | 0    | 连接到AGX |
| N305    | 1    | 1    | 连接到N305 |

**主要API：**
- `usb_mux_controller_init()` - 初始化USB MUX控制器
- `usb_mux_controller_set_target(target)` - 设置USB目标设备
- `usb_mux_controller_get_target(*target)` - 获取当前目标
- `usb_mux_controller_get_target_name(target)` - 获取目标名称

### 3. hardware_commands 组件
位置：`components/hardware_commands/`

**功能特性：**
- 提供GPIO和USB MUX的控制台命令
- 集成到console_core系统
- 中文友好的用户界面
- 完整的错误处理和帮助信息

## 控制台命令

### GPIO命令
```bash
gpio <pin> high|low|input
```

**参数说明：**
- `<pin>` - GPIO引脚号（0-48，仅限有效引脚）
- `high` - 设置GPIO为高电平输出
- `low` - 设置GPIO为低电平输出
- `input` - 设置GPIO为输入模式并读取电平

**使用示例：**
```bash
# 设置GPIO8为高电平
robOS> gpio 8 high
GPIO8 已设置为高电平

# 设置GPIO48为低电平  
robOS> gpio 48 low
GPIO48 已设置为低电平

# 读取GPIO8的输入状态
robOS> gpio 8 input
GPIO8 输入电平: 高
```

### USB MUX命令
```bash
usbmux esp32s3|agx|n305|status
```

**参数说明：**
- `esp32s3` - 切换USB-C接口到ESP32S3
- `agx` - 切换USB-C接口到AGX
- `n305` - 切换USB-C接口到N305
- `status` - 显示当前USB-C接口连接状态

**使用示例：**
```bash
# 切换到ESP32S3
robOS> usbmux esp32s3
USB-C接口已切换到ESP32S3

# 切换到AGX
robOS> usbmux agx
USB-C接口已切换到AGX

# 切换到N305
robOS> usbmux n305
USB-C接口已切换到N305

# 查看当前状态
robOS> usbmux status
当前USB-C接口连接到: ESP32S3
```

## GPIO安全使用原则

⚠️ **重要安全提示：**

1. **输出控制**：使用 `gpio <pin> high|low` 设置输出状态
2. **输入读取**：使用 `gpio <pin> input` 切换到输入模式并读取
3. **避免干扰**：不要在输出模式下进行状态读取，以防止GPIO状态干扰
4. **关键操作**：关键操作（如恢复模式）完全避免状态验证

## 系统集成

新组件已完全集成到robOS系统中：

1. **初始化顺序**：
   - Hardware HAL → GPIO Controller → USB MUX Controller → Console Core → Hardware Commands

2. **依赖关系**：
   - `usb_mux_controller` 依赖 `gpio_controller`
   - `hardware_commands` 依赖两个控制器组件和 `console_core`

3. **自动启动**：系统启动时自动初始化所有组件并注册控制台命令

## 构建状态

✅ **项目构建成功**

所有新组件已成功编译并链接，无编译错误或警告。

## 测试建议

在实际硬件上测试时，建议按以下顺序验证功能：

1. **GPIO基本测试**：
   ```bash
   gpio 8 high    # 测试MUX1引脚
   gpio 8 low
   gpio 48 high   # 测试MUX2引脚
   gpio 48 low
   ```

2. **USB MUX切换测试**：
   ```bash
   usbmux status       # 查看初始状态
   usbmux esp32s3     # 切换到ESP32S3
   usbmux agx         # 切换到AGX
   usbmux n305        # 切换到N305
   usbmux status      # 验证当前状态
   ```

3. **GPIO输入测试**：
   ```bash
   gpio 8 input       # 读取MUX1引脚状态
   gpio 48 input      # 读取MUX2引脚状态
   ```

## 版本信息

- **添加日期**：2025-10-03
- **robOS版本**：1.0.0-dev
- **ESP-IDF版本**：5.5.1
- **构建状态**：✅ 成功