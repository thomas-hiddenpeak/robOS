# 智能安全温度策略 - 设计文档

## 概述

为了确保robOS系统在各种运行状态下的安全性，我们实现了一个智能安全温度策略。该策略根据系统状态动态调整风扇控制的温度阈值，确保在数据丢失或系统异常时设备不会因过热而损坏。

## 设计原理

### 核心理念
- **安全优先**：宁可风扇转速过高，也不能让设备过热
- **分层保护**：根据不同的风险级别提供不同的温度阈值
- **智能判断**：基于时间戳和数据状态自动选择最合适的温度策略

## 温度策略层级

### 1. 手动模式（最高优先级）
- **触发条件**：用户执行 `temp set <温度>` 命令
- **温度来源**：用户设置的测试温度值
- **用途**：调试和测试风扇曲线
- **安全性**：由用户控制，适合开发调试阶段

### 2. AGX自动模式（系统运行时的分层保护）

#### 2.1 系统启动保护
- **触发条件**：系统启动后60秒内
- **安全温度**：**75°C**
- **目的**：系统启动阶段数据未稳定时提供充分散热
- **原理**：开机时各组件逐步启动，提前启动风扇防止瞬时过热

#### 2.2 AGX离线紧急保护
- **触发条件**：AGX从未连接或长时间离线
- **安全温度**：**85°C**
- **目的**：AGX不可用时的最高安全保护
- **原理**：无法获取真实温度时，使用最高温度确保风扇高速运转

#### 2.3 数据过期保护
- **触发条件**：AGX数据超过10秒未更新
- **安全温度**：**65°C**
- **目的**：AGX连接不稳定时的中等保护
- **原理**：数据可能滞后，使用偏高温度保证安全裕量

#### 2.4 正常运行模式
- **触发条件**：AGX数据新鲜（10秒内更新）
- **温度来源**：AGX实时CPU温度
- **目的**：正常运行时的精确控制
- **原理**：使用真实温度数据，实现最佳的散热效率

### 3. 系统备用保护
- **触发条件**：互斥锁超时或其他异常
- **安全温度**：**45°C**
- **目的**：系统异常时的最后保护
- **原理**：确保即使在系统故障时也有基本的风扇运转

## 技术实现

### 时间戳管理
```c
static uint64_t s_agx_last_update_time = 0;   // AGX数据最后更新时间
static uint64_t s_system_start_time = 0;      // 系统启动时间

// 在系统初始化时记录启动时间
s_system_start_time = esp_timer_get_time();

// 在接收AGX数据时更新时间戳
s_agx_last_update_time = esp_timer_get_time();
```

### 智能判断逻辑
```c
uint64_t current_time = esp_timer_get_time();
uint64_t time_since_startup = current_time - s_system_start_time;
uint64_t time_since_agx_update = current_time - s_agx_last_update_time;

if (time_since_startup < STARTUP_PROTECTION_TIME_US) {
    // 启动保护期
    *temperature = TEMP_STARTUP_PROTECTION;  // 75°C
} else if (s_agx_last_update_time == 0) {
    // AGX从未连接
    *temperature = TEMP_AGX_OFFLINE_EMERGENCY;  // 85°C
} else if (time_since_agx_update > AGX_DATA_STALE_TIME_US) {
    // 数据过期
    *temperature = TEMP_DATA_STALE_FALLBACK;  // 65°C
} else {
    // 正常运行
    *temperature = s_agx_temperature;  // 实际温度
}
```

### 安全参数配置
```c
#define TEMP_STARTUP_PROTECTION     75.0f  // 启动保护温度
#define TEMP_AGX_OFFLINE_EMERGENCY  85.0f  // AGX离线紧急温度
#define TEMP_DATA_STALE_FALLBACK    65.0f  // 数据过期回退温度
#define TEMP_FINAL_FALLBACK         45.0f  // 最终备用温度
#define STARTUP_PROTECTION_TIME_US  (60 * 1000000ULL)  // 启动保护时间：60秒
#define AGX_DATA_STALE_TIME_US      (10 * 1000000ULL)  // 数据过期时间：10秒
```

## 状态监控

### `temp status`命令增强显示
```bash
temp status
```

输出示例：

**启动保护期：**
```
Temperature Mode: AGX Auto
Effective Temperature: 75.0°C
Temperature Source: Startup Protection (High temp for 60s startup safety)
System Uptime: 45 seconds
AGX Data: Never received
```

**AGX离线：**
```
Temperature Mode: AGX Auto
Effective Temperature: 85.0°C
Temperature Source: AGX Offline Emergency (AGX never connected - safety mode)
System Uptime: 120 seconds
AGX Data: Never received
```

**数据过期：**
```
Temperature Mode: AGX Auto
Effective Temperature: 65.0°C
Temperature Source: Stale Data Fallback (AGX data >10s old - safety mode)
System Uptime: 300 seconds
AGX Data Age: 15 seconds (Raw: 42.5°C)
```

**正常运行：**
```
Temperature Mode: AGX Auto
Effective Temperature: 42.5°C
Temperature Source: AGX CPU (Live)
System Uptime: 300 seconds
AGX Data Age: 2 seconds (Raw: 42.5°C)
```

## 风险评估与对策

### 潜在风险
1. **功耗增加**：高温设置导致风扇长期高速运转
2. **噪音问题**：85°C对应高转速可能产生噪音
3. **组件寿命**：风扇长期高速运转可能影响寿命

### 对策措施
1. **分层设计**：根据风险等级设置不同温度，避免过度保护
2. **时间限制**：启动保护只持续60秒，避免长期高速运转
3. **状态监控**：提供详细状态信息，便于诊断和调优
4. **手动干预**：支持手动模式，允许用户在特殊情况下控制

## 使用建议

### 生产环境
- 使用默认配置，系统会自动处理各种异常情况
- 定期检查 `temp status` 确保AGX连接正常
- 监控风扇噪音和功耗，必要时调整温度曲线

### 开发调试
- 使用 `temp set` 命令测试不同温度点的风扇响应
- 通过 `temp status` 观察系统的温度策略状态
- 在AGX离线测试时，系统会自动进入安全模式

### 故障诊断
1. **风扇转速异常高**：检查是否在启动保护期或AGX离线
2. **温度响应不灵敏**：确认是否在数据过期保护模式
3. **系统过热**：检查AGX连接状态和数据更新频率

## 性能影响

- **CPU开销**：每次温度查询增加约20-30个CPU周期（时间戳比较）
- **内存开销**：增加16字节全局变量存储时间戳
- **响应延迟**：无明显影响，时间戳操作在微秒级别

## 配置建议

根据不同应用场景，可以调整安全温度参数：

### 高可靠性场景
```c
#define TEMP_STARTUP_PROTECTION     80.0f  // 更高的启动保护
#define TEMP_AGX_OFFLINE_EMERGENCY  90.0f  // 更高的离线保护
```

### 低噪音场景
```c
#define TEMP_STARTUP_PROTECTION     65.0f  // 降低启动噪音
#define TEMP_AGX_OFFLINE_EMERGENCY  75.0f  // 降低离线噪音
```

### 低功耗场景
```c
#define TEMP_DATA_STALE_FALLBACK    55.0f  // 降低数据过期温度
#define STARTUP_PROTECTION_TIME_US  (30 * 1000000ULL)  // 缩短保护时间
```

## 总结

这个智能安全温度策略实现了：

1. **多层次安全保护**：从启动到运行的全程保护
2. **智能状态判断**：基于时间和数据状态的自动切换
3. **详细状态监控**：便于诊断和维护的丰富信息
4. **灵活配置支持**：支持不同应用场景的参数调整

该策略在保证系统安全的前提下，最大化了运行效率和用户体验。

---

**版本**：2.0.0  
**更新日期**：2025-10-04  
**作者**：robOS Team