# 电源监控诊断指南

## 问题现象
电源监控组件已初始化并运行，但所有数据都显示为0：
- 电压: 0.00V
- 电流: 0.000A  
- 功率: 0.00W
- 采样次数: 0
- Power Chip数据包: 0

## 诊断步骤

### 1. 启用调试模式
首先启用调试模式以查看详细信息：
```bash
power debug enable
```

### 2. 测试ADC直接读取
使用新增加的测试命令验证ADC是否正常工作：
```bash
power test adc
```
这个命令会：
- 执行10次连续的ADC读取
- 显示原始ADC值、校准后的毫伏值和计算的实际电压
- 帮助确定ADC硬件是否工作正常

### 3. 检查系统日志
查看系统启动和运行时的日志信息，特别注意：
- ADC初始化消息
- 电压监控任务的调试输出
- 任何错误或警告信息

### 4. 验证采样间隔
检查当前的采样间隔设置：
```bash
power voltage interval
```
默认间隔是5000ms（5秒），如果太长可能看不到实时更新。

### 5. 手动触发电压读取
```bash
power voltage
```
这会立即读取一次电压数据，绕过定时采样。

## 可能的问题和解决方案

### 问题1: ADC初始化失败
**症状**: `power test adc` 返回错误
**解决方案**: 
- 检查GPIO18是否被其他组件占用
- 确认ESP32S3的ADC2是否可用（WiFi使用时ADC2可能不可用）

### 问题2: 电压分压电路问题
**症状**: ADC读取到值但计算的电压不正确
**解决方案**:
- 检查分压电阻是否正确连接
- 验证分压比配置（默认11.4:1）
- 用万用表测量分压电路输出

### 问题3: 采样任务未运行
**症状**: 任务状态显示运行，但采样次数始终为0
**解决方案**:
- 查看任务调试日志
- 检查采样间隔是否过长
- 验证任务是否被阻塞

### 问题4: 硬件连接问题
**症状**: 所有软件功能正常但读取值为0
**解决方案**:
- 用万用表验证GPIO18上的电压
- 检查电源电压是否在预期范围内
- 确认电路连接正确

## 硬件连接验证

### 电压监控连接
- **GPIO18** (ADC2_CHANNEL_7) ← 分压电路输出
- 分压电路: VIN --[R1]-- VOUT --[R2]-- GND
- 分压比: R1:R2 = 10.4:1 (总比11.4:1)
- 输入电压范围: 0-36V → ADC输入: 0-3.16V

### 功率芯片连接  
- **GPIO47** (UART1_RX) ← 功率芯片数据输出
- 波特率: 9600
- 数据格式: 8字节数据包

## 调试命令摘要

```bash
# 基础诊断
power                    # 查看状态概览
power debug enable       # 启用调试模式
power test adc          # 测试ADC读取

# 配置检查
power config show       # 显示完整配置
power voltage interval  # 检查采样间隔

# 数据验证
power voltage           # 立即读取电压
power chip             # 显示功率芯片数据
power stats            # 详细统计信息

# 系统控制
power start            # 启动监控
power stop             # 停止监控
power reset            # 重置统计
```

## 预期的正常日志输出

启用调试模式后，应该看到类似的日志：

```
I (12345) power_monitor: Attempting voltage sample (interval: 5000 ms)
I (12345) power_monitor: ADC raw reading: 1234 (handle: 0x3fcxxxxx)
I (12345) power_monitor: Calibrated voltage: 987 mV
I (12345) power_monitor: Supply voltage: raw=1234, mv=987, actual=11.25V, divider=11.4
```

如果看不到这些日志，说明采样任务可能没有正常运行。

## 常见错误信息

- `Power monitor not initialized` - 需要先初始化
- `Failed to read supply voltage ADC` - ADC硬件问题
- `Failed to calibrate supply voltage` - 校准失败，使用线性转换
- `ADC raw reading: 0` - 可能是硬件连接或配置问题

## 进一步调试

如果上述步骤都无法解决问题，请：

1. 收集完整的系统启动日志
2. 记录 `power test adc` 的完整输出
3. 用万用表测量实际硬件电压
4. 检查是否有其他组件冲突使用GPIO18或ADC2