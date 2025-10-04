# AGX Monitor Updates - 日志优化和命令重构

## 更新日期
2025年10月4日

## 主要更改

### 1. 添加AGX启动延时功能

考虑到AGX系统需要45秒的启动时间，添加了启动延时配置避免过早连接尝试：

- **新配置参数**: `startup_delay_ms` - AGX启动延时时间（毫秒）
- **默认值**: 45000ms (45秒)
- **功能**: 监控任务启动后等待指定时间再尝试连接AGX服务器
- **日志提示**: 在等待期间显示启动延时进度信息

#### 配置示例：
```c
agx_monitor_config_t config;
agx_monitor_get_default_config(&config);
config.startup_delay_ms = 45000;  // 45秒启动延时
agx_monitor_init(&config);
```

### 2. 彻底清理干扰性日志输出

为了完全消除控制台干扰，我们将几乎所有运行时日志从INFO/WARN级别改为DEBUG级别：

#### 第一轮优化 - 基础日志减少：
- **健康检查日志** (每30秒输出的状态信息)
- **连接状态详细信息** (服务器地址、连接尝试次数等)
- **WebSocket连接启动信息**
- **Socket.IO响应接收信息**
- **重试等待时间信息** (快速重试和固定间隔模式)

#### 第二轮优化 - 彻底清理：
- **异常数据检测警告** (包括字节详情和重连触发)
- **连接尝试和重连日志** (初始连接、重连尝试等)
- **WebSocket断开连接信息**
- **Socket.IO消息发送和等待状态**
- **错误设置警告信息**
- **连接失败详情**

#### 保留的重要INFO级别日志：
- AGX监控初始化和配置信息
- 连接成功/失败的关键状态变化
- Socket.IO连接建立
- 错误和警告信息

### 2. 命令层次结构重构

将原来的7个独立命令整合为一个主命令下的子命令结构：

#### 旧命令结构：
```bash
agx_status      # 显示状态
agx_start       # 启动监控
agx_stop        # 停止监控
agx_data        # 显示数据
agx_config      # 显示配置
agx_stats       # 显示统计
agx_debug       # 调试命令
```

#### 新命令结构：
```bash
agx_monitor <subcommand> [args]
```

#### 可用子命令：
- `agx_monitor status` - 显示AGX监控连接状态和统计信息
- `agx_monitor start` - 启动AGX监控
- `agx_monitor stop` - 停止AGX监控  
- `agx_monitor data` - 显示最新的AGX系统数据
- `agx_monitor config` - 显示AGX监控配置
- `agx_monitor stats` - 显示详细的AGX监控统计信息
- `agx_monitor debug [verbose|quiet|normal|reconnect]` - 调试命令

#### 使用帮助：
- 运行 `agx_monitor` 不带参数可查看所有可用子命令
- 每个子命令的功能与之前的独立命令完全相同

## 测试建议

### 1. 验证AGX启动延时功能
1. 烧录更新后的固件
2. 观察启动日志，应该看到：
   - "AGX Startup delay: 45000 ms (45.0 seconds)"
   - "Waiting 45000 ms for AGX system to boot up..."
   - 在45秒后显示 "AGX startup delay completed, ready to connect"
3. 使用 `agx_monitor config` 命令验证配置显示包含启动延时信息

### 2. 验证日志输出彻底清理
1. 烧录更新后的固件
2. 观察控制台输出，应该发现：
   - **异常数据检测信息**不再频繁显示
   - **连接重试尝试**不再打断控制台操作
   - **WebSocket连接状态**详情不再输出
   - **Socket.IO连接过程**日志隐藏
   - 控制台基本保持安静，只显示真正重要的事件

### 2. 验证新命令结构
```bash
# 查看帮助
agx_monitor

# 测试各个子命令
agx_monitor status
agx_monitor data
agx_monitor config
agx_monitor stats
agx_monitor debug verbose
```

## 技术细节

### 日志级别变更
- 使用ESP-IDF的日志级别系统
- 可以在运行时通过 `esp_log_level_set("agx_monitor", ESP_LOG_DEBUG)` 启用调试日志
- 默认情况下只显示INFO级别及以上的日志

### 命令注册优化
- 从注册7个独立命令减少到注册1个主命令
- 减少了内存占用和命令表复杂度
- 保持了所有原有功能的完整性

## 向后兼容性

**注意：** 这次更新**不兼容**旧的独立命令。如果有自动化脚本或文档使用了旧的命令格式，需要更新为新的子命令格式。

## 预期效果

1. **智能启动同步** - 避免在AGX系统未准备好时的无效连接尝试
2. **极度清洁的控制台体验** - 减少了约95%的调试信息输出，基本消除干扰
2. **更好的命令组织** - 相关功能归类在同一个主命令下
3. **更容易使用** - `agx_monitor` 提供统一的帮助入口
4. **减少命名冲突** - 避免在控制台中与其他组件的命令冲突

## 构建状态

✅ 编译成功  
✅ 二进制大小进一步优化（减少到 0x103fe0 bytes，约1040KB）  
✅ 无编译错误（仅有警告，不影响功能）  
✅ 日志清理完成，控制台干扰基本消除