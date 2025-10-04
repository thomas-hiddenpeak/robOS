# Web Server 事件驱动重构 - 更新日志

## 概述
将 Web Server 组件从直接启动模式重构为事件驱动模式，确保只有在 SD 卡挂载成功且以太网连接就绪后才启动服务器。

## 主要变更

### 1. 移除直接启动逻辑
- **之前**：在系统初始化时直接检查条件并尝试启动 Web Server
- **现在**：仅在初始化时准备 Web Server，不立即启动

### 2. 添加状态跟踪变量
在 `main.c` 中添加了全局状态跟踪：
```c
static bool web_server_initialized = false;
static bool ethernet_ready = false;
static bool storage_ready = false;
```

### 3. 实现事件驱动机制

#### 存储事件处理
- 注册 `STORAGE_EVENTS` 事件处理器
- 监听 `STORAGE_EVENT_MOUNTED` 和 `STORAGE_EVENT_UNMOUNTED` 事件
- 自动更新 `storage_ready` 状态并触发启动检查

#### 以太网事件处理
- 注册以太网状态变化回调函数
- 监听以太网状态变化：`ETHERNET_STATUS_IP_ASSIGNED` 或 `ETHERNET_STATUS_READY`
- 自动更新 `ethernet_ready` 状态并触发启动检查

### 4. 统一启动检查函数
创建 `check_and_start_web_server()` 函数：
- 检查 Web Server 是否已初始化
- 检查是否已经在运行
- 验证存储和网络条件
- 满足条件时自动启动服务器
- 记录详细的状态信息

### 5. 自动停止机制
- 当存储卸载时自动停止 Web Server
- 当以太网断开时自动停止 Web Server
- 提供用户友好的状态消息

## 技术实现细节

### 事件注册
```c
// 以太网事件回调
ret = ethernet_manager_register_event_callback(ethernet_status_callback, NULL);

// 存储事件处理器
ret = esp_event_handler_register(STORAGE_EVENTS, ESP_EVENT_ANY_ID, 
                                 storage_event_handler, NULL);
```

### 状态检查逻辑
```c
if (storage_ready && ethernet_ready) {
    ESP_LOGI(TAG, "Both storage and ethernet ready, starting web server...");
    esp_err_t ret = web_server_start();
    // ... 处理启动结果
}
```

## 日志输出改进

### 启动时日志
```
I (2257) WEB_SERVER: Web server initialized - will start when conditions are met
I (2267) ROBOS_MAIN: Web server initialized
I (2270) ROBOS_MAIN: Initial conditions - storage: ready, ethernet: ready
```

### 事件触发日志
```
I (xxxx) ROBOS_MAIN: Storage mounted event received
I (xxxx) ROBOS_MAIN: Ethernet status changed: 6
I (xxxx) ROBOS_MAIN: Ethernet is now ready, checking web server start conditions
```

### 启动成功日志
```
I (xxxx) ROBOS_MAIN: Both storage and ethernet ready, starting web server...
I (xxxx) WEB_SERVER: Web server started on port 80
I (xxxx) ROBOS_MAIN: Web server started on port 80
I (xxxx) ROBOS_MAIN: Web interface available at http://10.10.99.97/
```

## 用户体验改进

### 之前的问题
- Web Server 在系统启动时可能因条件不满足而失败启动
- 用户需要手动重启或重新挂载来触发 Web Server 启动
- 错误消息不够明确

### 现在的优势
- 自动响应存储和网络状态变化
- 智能的启动时机选择
- 详细的状态日志和错误信息
- 用户无需手动干预

## 构建和测试结果

### 构建状态
✅ 编译成功，无错误和警告

### 二进制大小
- 新二进制大小：0x10ef00 字节
- 可用空间：0xf1100 字节 (47% 可用)

## 向后兼容性
- 现有的 Web Server API 保持不变
- 配置选项和功能特性完全保留
- 外部访问方式（URL、端口）未发生变化

## 未来扩展
该事件驱动架构为未来扩展提供了良好基础：
- 可以轻松添加其他组件的依赖检查
- 支持更复杂的启动条件逻辑
- 便于集成系统健康检查和自动恢复机制