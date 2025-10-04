# AGX启动延时功能实现

## 概述
AGX系统需要45秒的启动时间才能完全就绪。为了避免在AGX未准备好时进行无效的连接尝试，实现了启动延时功能。

## 实现细节

### 1. 配置结构扩展
- **新字段**: `startup_delay_ms` (uint32_t)
- **默认值**: 45000ms (45秒)
- **位置**: `agx_monitor_config_t` 结构体

### 2. 核心实现逻辑
在AGX监控任务 (`agx_monitor_task`) 中实现启动延时：

```c
// 启动延时状态跟踪
bool startup_delay_completed = false;
uint64_t task_start_time = esp_timer_get_time();

// 延时检查循环
while (s_agx_monitor.running) {
    if (!startup_delay_completed && s_agx_monitor.config.startup_delay_ms > 0) {
        uint64_t elapsed_ms = (esp_timer_get_time() - task_start_time) / 1000;
        if (elapsed_ms < s_agx_monitor.config.startup_delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(5000)); // 每5秒检查一次
            continue; // 跳过连接尝试
        }
        startup_delay_completed = true;
    }
    
    // 只有延时完成后才尝试连接
    // ... 连接逻辑 ...
}
```

### 3. 用户界面更新
- **配置显示**: `agx_monitor config` 命令显示启动延时设置
- **初始化日志**: 启动时显示延时配置信息
- **运行时日志**: 延时期间显示等待状态

### 4. 日志输出示例

**初始化时**:
```
I (1234) agx_monitor: AGX Startup delay: 45000 ms (45.0 seconds)
```

**任务启动时**:
```
I (1234) agx_monitor: Waiting 45000 ms for AGX system to boot up...
```

**延时完成时**:
```
I (46234) agx_monitor: AGX startup delay completed, ready to connect
```

## 配置方法

### 使用默认配置（推荐）
```c
agx_monitor_config_t config;
agx_monitor_get_default_config(&config); // startup_delay_ms = 45000
agx_monitor_init(&config);
```

### 自定义延时时间
```c
agx_monitor_config_t config;
agx_monitor_get_default_config(&config);
config.startup_delay_ms = 60000; // 设置为60秒
agx_monitor_init(&config);
```

### 禁用启动延时
```c
agx_monitor_config_t config;
agx_monitor_get_default_config(&config);
config.startup_delay_ms = 0; // 禁用延时
agx_monitor_init(&config);
```

## 优势

1. **避免无效连接**: 不会在AGX未就绪时浪费连接尝试
2. **减少错误日志**: 减少因AGX未启动导致的连接失败日志
3. **提高系统稳定性**: 确保在AGX完全启动后再建立监控连接
4. **灵活配置**: 可根据不同AGX系统的启动时间调整延时
5. **智能同步**: 实现ESP32和AGX系统的启动同步

## 测试要点

1. **延时生效**: 验证系统启动后等待指定时间才开始连接
2. **配置显示**: 检查 `agx_monitor config` 命令正确显示延时设置
3. **日志正确**: 确认启动延时相关的日志信息正确输出
4. **连接时机**: 验证延时结束后立即开始连接尝试
5. **配置灵活性**: 测试不同延时值的正确性

## 兼容性

- **向后兼容**: 现有代码无需修改，默认启用45秒延时
- **可选功能**: 可通过设置 `startup_delay_ms = 0` 禁用
- **配置保持**: 延时设置在整个运行期间保持不变

## 注意事项

1. 延时只在任务首次启动时生效，重连时不会重新延时
2. 延时期间系统仍在运行，只是跳过连接尝试
3. 可通过监控命令随时查看当前配置的延时设置
4. 延时时间建议根据实际AGX系统启动时间设置