# Power Monitor Component

板载电源监测组件，为 robOS 提供全面的电源监控功能。

## 功能特性

### 🔋 电压监控
- **ADC采样**: GPIO 18 (ADC2_CHANNEL_7)，12位分辨率
- **分压检测**: 11.4:1 分压比，支持 0-37.4V 高电压检测
- **实时监控**: 可配置采样间隔 (100ms-60s)
- **阈值报警**: 可设置最小/最大电压阈值，超出时触发事件
- **统计信息**: 自动计算平均电压、采样次数等统计数据

### ⚡ 电源芯片通信
- **UART接口**: GPIO 47 (UART1_RX)，9600波特率，8N1配置
- **协议解析**: 支持 `[0xFF帧头][电压][电流][CRC]` 4字节数据格式
- **数据验证**: 自动CRC校验，确保数据完整性
- **协议调试**: 可开启详细的协议分析和调试信息
- **超时处理**: 配置超时检测，统计通信错误

### 📊 数据管理
- **实时数据**: 提供最新的电压、电流、功率读数
- **历史统计**: 运行时间、采样次数、错误统计等
- **事件回调**: 支持阈值超出、CRC错误、超时等事件通知
- **配置持久化**: 支持配置保存到NVS (待实现)

### 🎛️ 控制台命令
提供10个专用控制台命令，支持完整的配置和监控功能：

| 命令 | 功能 | 示例用法 |
|------|------|----------|
| `power` | 显示电源监控状态 | `power` |
| `power start` | 启动电源监控 | `power start` |
| `power stop` | 停止电源监控 | `power stop` |
| `power config` | 配置管理 | `power config show` |
| `power thresholds` | 阈值设置 | `power thresholds 10.0 30.0` |
| `power debug` | 调试模式 | `power debug enable` |
| `power stats` | 详细统计 | `power stats` |
| `power reset` | 重置统计 | `power reset` |
| `power voltage` | 电压监控 | `power voltage interval 500` |
| `power chip` | 电源芯片数据 | `power chip` |

## 硬件配置

### 电压监控电路
```
外部电压 ──┬── 100kΩ ──┬── GPIO18 (ADC)
           │           │
         负载         10kΩ
           │           │
          GND ────────┴── GND

分压比 = (100kΩ + 10kΩ) / 10kΩ = 11:1 实际测得为 11.4:1
最大检测电压 = 3.3V × 11.4 = 37.62V
```

### UART通信接口
```
电源芯片 TX ──── GPIO47 (UART1_RX) ──── ESP32S3
波特率: 9600, 8N1, 无流控
数据格式: [0xFF][电压][电流][CRC]
```

## API 接口

### 初始化和控制
```c
// 获取默认配置
esp_err_t power_monitor_get_default_config(power_monitor_config_t *config);

// 初始化组件
esp_err_t power_monitor_init(const power_monitor_config_t *config);

// 启动/停止监控
esp_err_t power_monitor_start(void);
esp_err_t power_monitor_stop(void);

// 反初始化
esp_err_t power_monitor_deinit(void);
```

### 数据读取
```c
// 获取电压数据
esp_err_t power_monitor_get_voltage_data(voltage_data_t *data);

// 获取电源芯片数据
esp_err_t power_monitor_get_power_chip_data(power_chip_data_t *data);

// 获取统计信息
esp_err_t power_monitor_get_stats(power_monitor_stats_t *stats);
```

### 配置管理
```c
// 阈值设置
esp_err_t power_monitor_set_voltage_thresholds(float min_voltage, float max_voltage);
esp_err_t power_monitor_get_voltage_thresholds(float *min_voltage, float *max_voltage);

// 采样间隔
esp_err_t power_monitor_set_sample_interval(uint32_t interval_ms);
esp_err_t power_monitor_get_sample_interval(uint32_t *interval_ms);

// 事件回调
esp_err_t power_monitor_register_callback(power_monitor_event_callback_t callback, void *user_data);
```

## 使用示例

### 基本使用
```c
#include "power_monitor.h"

// 获取默认配置
power_monitor_config_t config;
power_monitor_get_default_config(&config);

// 可选：修改配置
config.voltage_config.sample_interval_ms = 500;  // 500ms采样间隔
config.voltage_config.voltage_min_threshold = 12.0f;  // 12V最小阈值
config.voltage_config.voltage_max_threshold = 24.0f;  // 24V最大阈值

// 初始化并启动
power_monitor_init(&config);
power_monitor_start();

// 读取数据
voltage_data_t voltage_data;
if (power_monitor_get_voltage_data(&voltage_data) == ESP_OK) {
    printf("当前电压: %.2fV\n", voltage_data.voltage_v);
}

power_chip_data_t power_data;
if (power_monitor_get_power_chip_data(&power_data) == ESP_OK) {
    printf("功率: %.2fW (%.2fV × %.3fA)\n", 
           power_data.power_w, power_data.voltage_v, power_data.current_a);
}
```

### 事件回调
```c
void power_event_handler(power_monitor_event_type_t event_type, void *event_data, void *user_data) {
    switch (event_type) {
        case POWER_MONITOR_EVENT_VOLTAGE_THRESHOLD:
            voltage_data_t *data = (voltage_data_t*)event_data;
            printf("⚠️ 电压阈值报警: %.2fV\n", data->voltage_v);
            break;
            
        case POWER_MONITOR_EVENT_POWER_DATA_RECEIVED:
            power_chip_data_t *power = (power_chip_data_t*)event_data;
            if (power->crc_valid) {
                printf("📊 电源数据: %.2fV, %.3fA, %.2fW\n", 
                       power->voltage_v, power->current_a, power->power_w);
            }
            break;
            
        case POWER_MONITOR_EVENT_CRC_ERROR:
            printf("❌ CRC校验错误\n");
            break;
    }
}

// 注册回调
power_monitor_register_callback(power_event_handler, NULL);
```

## 控制台使用示例

### 基本状态查看
```bash
robOS> power
Power Monitor Status:
=====================
Initialized: Yes
Running: Yes

Voltage Monitoring:
  Current Voltage: 13.25V
  ADC Raw Value: 1580
  Threshold Alarm: OK
  Thresholds: 10.00V - 30.00V
  Sample Interval: 1000ms

Power Chip Data:
  Voltage: 13.20V
  Current: 2.150A
  Power: 28.38W
  CRC Valid: Yes
  Raw Data: FF 84 D7 53

Statistics:
  Uptime: 45230ms
  Voltage Samples: 45
  Power Chip Packets: 12
  CRC Errors: 0
  Timeout Errors: 2
  Threshold Violations: 0
  Average Voltage: 13.18V
  Average Current: 2.125A
  Average Power: 27.98W
```

### 配置管理
```bash
# 设置电压阈值
robOS> power thresholds 12.0 24.0
Voltage thresholds set: 12.00V - 24.00V

# 修改采样间隔
robOS> power voltage interval 500
Sample interval set to 500ms

# 启用协议调试
robOS> power debug enable
Protocol debug enabled

# 查看详细统计
robOS> power stats
Power Monitor Statistics:
========================
Uptime: 120540 ms (0.0 hours)
Voltage Samples: 241
Power Chip Packets: 67
CRC Errors: 2 (3.0%)
Timeout Errors: 5
Threshold Violations: 1
Average Voltage: 13.45V
Average Current: 1.875A
Average Power: 25.22W
```

## 技术规格

| 参数 | 规格 | 说明 |
|------|------|------|
| ADC分辨率 | 12位 (0-4095) | ESP32S3内置ADC |
| 电压测量范围 | 0-37.4V | 基于3.3V参考电压和11.4:1分压 |
| 测量精度 | ±0.1V | 典型值，受分压电阻精度影响 |
| 采样频率 | 0.017-10Hz | 可配置间隔100ms-60s |
| UART波特率 | 9600 bps | 固定，8N1配置 |
| 内存占用 | ~8KB | 包含任务栈和数据缓冲区 |
| CPU占用 | <1% | 后台任务，低优先级 |

## 故障排除

### 常见问题

**Q: 电压读数不准确**
- 检查分压电阻阻值（应为100kΩ和10kΩ）
- 验证ADC参考电压是否为3.3V
- 确认GPIO18连接正确且无短路

**Q: 无法接收电源芯片数据**
- 检查GPIO47的UART1_RX连接
- 确认电源芯片TX输出正常
- 使用示波器检查信号质量和波特率

**Q: CRC错误率过高**
- 检查UART连接线缆质量
- 确认波特率匹配（9600）
- 检查电源芯片数据格式是否正确

**Q: 阈值报警频繁触发**
- 调整阈值范围，避免临界值附近振荡
- 增加采样间隔减少噪声影响
- 检查电源稳定性

### 调试工具
```bash
# 启用详细调试信息
robOS> power debug enable

# 查看原始数据包
robOS> power chip

# 监控阈值事件
robOS> power thresholds enable

# 重置统计数据
robOS> power reset
```

## 开发状态

### ✅ 已完成功能
- [x] 电压监控 - ADC采集和分压计算
- [x] 电源芯片通信 - UART接收和协议解析
- [x] 阈值监控 - 可配置报警机制
- [x] 统计信息 - 完整的运行统计
- [x] 控制台命令 - 10个专用命令
- [x] 事件回调 - 异步事件通知
- [x] 单元测试 - 12个测试用例

### 🚧 待完善功能
- [ ] NVS配置持久化 - 配置自动保存和恢复
- [ ] 高级数据分析 - 趋势分析、峰值检测
- [ ] Web界面集成 - 通过以太网提供Web监控
- [ ] 数据记录功能 - 历史数据保存到SD卡

### 🔧 技术债务
- 电源芯片数据格式可能需要根据实际硬件调整
- CRC算法可能需要匹配具体的电源芯片规范
- 错误恢复机制需要进一步测试和优化

## 依赖组件

- `hardware_hal` - ADC和UART硬件抽象
- `console_core` - 控制台命令支持  
- `config_manager` - 配置管理支持
- `event_manager` - 事件通信支持
- ESP-IDF系统组件 - driver, esp_adc, freertos

## 版本历史

- **v1.0.0** (2025-10-02) - 初始版本，基础功能完整实现