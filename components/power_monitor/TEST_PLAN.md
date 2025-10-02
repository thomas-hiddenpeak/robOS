# 电源监控修复测试计划

## 已修复的问题

1. **统计更新问题**: 修复了`start_time_us`被`memset`意外重置的问题
2. **任务调试**: 添加了详细的任务运行日志
3. **ADC测试**: 添加了直接ADC测试功能
4. **调试信息**: 增加了综合调试信息命令

## 测试指令序列

请按以下顺序测试新的修复：

### 1. 基础调试信息检查
```bash
power debug info
```
**预期结果**: 
- 显示所有句柄和配置信息
- 显示实时ADC读取结果
- 显示开始时间和当前时间

### 2. 直接ADC测试
```bash
power test adc
```
**预期结果**:
- 显示10次连续的ADC读取
- 每次显示原始值、毫伏值、实际电压
- 如果硬件正常，应该看到非零的读数

### 3. 检查任务运行状态
```bash
power
```
**预期结果**:
- Uptime应该显示非零值（如果任务正在运行）
- 如果电压采样正常，Voltage Samples应该大于0

### 4. 启用详细调试日志
```bash
power debug enable
```
然后等待几秒查看系统日志输出

### 5. 重新启动监控（如果需要）
```bash
power stop
power start
```

## 关键日志信息

启动后您应该看到类似的日志：

```
I (xxxx) power_monitor: Power monitor initialized with start time: xxxxxxx us
I (xxxx) power_monitor: Power monitor started (task created)
I (xxxx) power_monitor: Power monitor task started - Running flag: true
I (xxxx) power_monitor: Power monitor task loop iteration
I (xxxx) power_monitor: Attempting voltage sample (interval: 5000 ms)
I (xxxx) power_monitor: ADC raw reading: xxxx (handle: 0x3fcxxxxx)
```

## 诊断步骤

1. **如果 `power debug info` 显示 ADC Handle 为 NULL**:
   - ADC初始化失败
   - 可能是GPIO冲突或ADC2被WiFi占用

2. **如果 `power test adc` 总是返回0**:
   - 硬件连接问题
   - GPIO18没有连接到分压电路

3. **如果 Uptime 仍然为0**:
   - 任务可能没有启动
   - 检查任务句柄是否为NULL

4. **如果 Voltage Samples 为0**:
   - 采样间隔太长或采样失败
   - 检查任务是否卡在某个地方

## 额外的测试命令

```bash
# 显示完整配置
power config show

# 检查详细统计
power stats

# 设置更短的采样间隔进行测试
power voltage interval 1000

# 手动读取电压
power voltage
```

## 期望结果

修复后，您应该看到：
- `power debug info` 显示所有句柄都不为NULL
- `power test adc` 显示实际的ADC读数
- `power` 命令显示非零的uptime和采样次数
- 系统日志中出现任务心跳和ADC读取信息

如果这些测试都通过了，但电压值仍然为0，那么问题很可能是硬件连接或ESP32S3的ADC2与WiFi冲突。