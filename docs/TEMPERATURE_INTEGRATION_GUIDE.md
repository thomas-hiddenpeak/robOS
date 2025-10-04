# AGX温度集成到风扇控制系统指南

## 概述

本指南介绍了如何在robOS系统中使用AGX CPU温度数据自动控制风扇，同时保持原有的调试功能。

## 功能特性

### 温度数据源优先级系统

系统采用智能优先级机制来选择温度数据源：

1. **手动模式**（最高优先级）
   - 使用 `temp set <温度>` 命令设置的测试温度
   - 用于调试和测试风扇曲线
   - 设置后自动启用手动模式

2. **AGX自动模式**（中等优先级）
   - 使用AGX监控器实时采集的CPU温度
   - 用于生产环境的自动风扇控制
   - 当手动模式关闭时自动生效

3. **默认模式**（最低优先级）
   - 使用固定的25°C作为默认值
   - 当AGX数据不可用时的备用方案

## 新增命令

### `temp` 命令（推荐使用）

```bash
# 设置手动测试温度（自动启用手动模式）
temp set 45

# 获取当前有效温度和数据源
temp get

# 切换到AGX自动模式
temp auto

# 切换到手动模式
temp manual

# 查看温度管理状态
temp status
```

### `test temp` 命令（兼容性保留）

为保持向后兼容性，原有的 `test temp <温度>` 命令仍然可用，会自动重定向到新的 `temp set` 命令。

## 使用场景

### 场景1：调试风扇曲线

```bash
# 1. 设置手动测试温度
temp set 30

# 2. 设置风扇为曲线模式
fan mode 0 curve

# 3. 配置温度曲线
fan config curve 0 25:10 35:30 45:60 55:100

# 4. 测试不同温度点
temp set 25    # 风扇应该10%转速
temp set 35    # 风扇应该30%转速
temp set 45    # 风扇应该60%转速
temp set 55    # 风扇应该100%转速

# 5. 检查当前温度状态
temp status
```

### 场景2：生产环境自动控制

```bash
# 1. 启动AGX监控器
agx_monitor start

# 2. 切换到AGX自动模式
temp auto

# 3. 设置风扇为曲线模式
fan mode 0 curve

# 4. 配置适合的温度曲线
fan config curve 0 40:20 50:40 60:70 70:100

# 5. 检查状态
temp status
agx_monitor status
fan status
```

### 场景3：在两种模式间切换

```bash
# 从自动模式切换到手动调试
temp manual
temp set 50

# 从手动模式切换回自动
temp auto

# 检查当前模式和温度源
temp status
```

## 系统集成

### AGX监控器自动推送

当AGX监控器接收到新的CPU温度数据时，会自动推送到温度管理系统：

```c
// AGX监控器会自动调用
console_set_agx_temperature(cpu_temperature);
```

### 风扇控制器智能选择

风扇控制器使用新的智能温度选择函数：

```c
// 自动选择最高优先级的温度源
console_get_effective_temperature(&temperature, &source);
```

## 状态监控

### 温度状态查看

```bash
temp status
```

输出示例：
```
Temperature Mode: AGX Auto
Effective Temperature: 42.5°C
Temperature Source: AGX CPU
```

### 风扇状态查看

```bash
fan status
```

输出示例：
```
Fan 0: Enabled, Auto-Curve, Speed: 45%, Temp: 42.5°C
```

## 故障排除

### 温度数据不更新

1. 检查AGX监控器状态：
   ```bash
   agx_monitor status
   ```

2. 检查温度源状态：
   ```bash
   temp status
   ```

3. 检查是否在手动模式：
   ```bash
   temp auto  # 切换到自动模式
   ```

### 风扇不响应温度变化

1. 确认风扇在曲线模式：
   ```bash
   fan mode 0 curve
   ```

2. 检查温度曲线配置：
   ```bash
   fan config show 0
   ```

3. 手动测试温度响应：
   ```bash
   temp set 60
   fan status
   ```

## 技术细节

### 线程安全

所有温度管理操作都使用互斥锁保护，确保多线程环境下的数据一致性。

### 性能影响

- AGX温度数据推送频率：1Hz
- 风扇控制器更新频率：基于配置的更新间隔
- 温度获取操作延迟：<100ms

### 内存使用

- 新增温度管理变量：约16字节
- 互斥锁开销：约100字节
- 总额外内存使用：<200字节

## 配置持久化

- 手动/自动模式状态：运行时状态，重启后重置为自动模式
- 风扇曲线配置：自动保存到NVS存储
- AGX监控器配置：保存到NVS存储

## 升级兼容性

- 原有的 `test temp` 命令完全兼容
- 现有风扇配置无需修改
- 升级后默认为AGX自动模式

## 最佳实践

1. **开发调试时**：使用 `temp set` 进行手动测试
2. **生产部署时**：使用 `temp auto` 启用自动控制
3. **定期检查**：使用 `temp status` 和 `fan status` 监控系统状态
4. **曲线调优**：基于实际工作负载调整温度曲线参数

---

**更新日期**：2025-10-04  
**版本**：1.0.0  
**作者**：robOS Team