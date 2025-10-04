# Web Server 组件使用说明

## 概述

Web Server 组件为 robOS 系统提供了完整的 HTTP 服务器功能，用于：

- 从 SD 卡 `/sdcard/web` 目录提供静态文件服务
- 提供 REST API 接口用于系统监控
- 支持网络状态监控界面

## 功能特性

### 静态文件服务
- 自动从 `/sdcard/web` 目录服务 HTML、CSS、JavaScript 文件
- 支持多种 MIME 类型检测
- 默认首页：`index.htm`

### API 接口
- `/api/network` - 返回网络状态 JSON 数据
- 支持 CORS 跨域访问
- JSON 格式响应

### 支持的文件类型
- HTML (`.html`, `.htm`) - `text/html`
- CSS (`.css`) - `text/css`
- JavaScript (`.js`) - `application/javascript`
- JSON (`.json`) - `application/json`
- 图片 (`.png`, `.jpg`, `.jpeg`, `.gif`, `.ico`, `.svg`)
- 其他文件 - `application/octet-stream`

## 系统集成

### 事件驱动启动机制
Web Server 采用事件驱动的启动机制，只有在以下条件都满足时才会自动启动：
1. SD 卡成功挂载 (`STORAGE_EVENT_MOUNTED`)
2. 以太网连接就绪 (`ETHERNET_STATUS_IP_ASSIGNED` 或 `ETHERNET_STATUS_READY`)

系统会自动监听相关事件：
- **存储事件**：监听 `STORAGE_EVENTS` 事件，当收到 `STORAGE_EVENT_MOUNTED` 时更新存储状态
- **以太网事件**：通过回调函数监听以太网状态变化，当状态变为就绪时更新网络状态
- **自动启动**：当两个条件都满足时，系统会自动启动 Web Server
- **自动停止**：当存储卸载或网络断开时，Web Server 会自动停止

### 默认配置
- 端口：80
- Web 根目录：`/sdcard/web`
- 默认页面：`index.htm`
- 启用 CORS：是

## Web 界面

当前实现的 web 界面提供网络监控功能：

### 访问地址
- 主页：`http://10.10.99.97/`
- API：`http://10.10.99.97/api/network`

### 界面功能
- 实时网络状态显示
- 自动刷新（每秒）
- 手动刷新按钮
- 响应时间监控
- 丢包率统计

## 配置和定制

### 修改配置
可以通过传递自定义配置到 `web_server_init()` 来修改服务器设置：

```c
web_server_config_t config = {
    .port = 8080,                    // 自定义端口
    .max_uri_handlers = 16,          // URI 处理器数量
    .max_resp_headers = 8,           // 最大响应头数量
    .max_open_sockets = 7,           // 最大开放socket数
    .enable_cors = true,             // 启用CORS
    .webroot_path = "/sdcard/web"    // Web根目录
};
web_server_init(&config);
```

### 添加新的API接口
在 `web_server.c` 中添加新的处理函数并注册到服务器：

```c
// 添加新的处理函数
static esp_err_t api_status_handler(httpd_req_t *req) {
    // 实现逻辑
}

// 在 web_server_start() 中注册
httpd_uri_t api_status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
    .user_ctx = NULL
};
httpd_register_uri_handler(server, &api_status_uri);
```

## 测试和验证

### 启动验证
系统启动后，您应该看到以下日志信息：
```
I (2257) WEB_SERVER: Web server initialized - will start when conditions are met
I (2267) ROBOS_MAIN: Web server initialized
I (2270) ROBOS_MAIN: Initial conditions - storage: ready, ethernet: ready
I (2280) ROBOS_MAIN: Both storage and ethernet ready, starting web server...
I (2290) WEB_SERVER: Web server started on port 80
I (2295) ROBOS_MAIN: Web server started on port 80
I (2300) ROBOS_MAIN: Web interface available at http://10.10.99.97/
```

### 事件响应测试
- **存储测试**：插拔 SD 卡应该触发 Web Server 的自动启动/停止
- **网络测试**：断开/连接以太网线应该触发相应的状态变化

### 功能测试
1. 访问 `http://10.10.99.97/` 应该显示网络监控界面
2. 访问 `http://10.10.99.97/api/network` 应该返回 JSON 格式的网络状态数据

## 故障排除

### 常见问题

1. **Web Server 无法启动**
   - 检查日志中的条件状态：`Initial conditions - storage: X, ethernet: Y`
   - 确认 SD 卡正确挂载：日志中应显示 `Storage mounted event received`
   - 确认以太网已连接：日志中应显示 `Ethernet status changed: X` (X >= 5)
   - 查看系统日志获取详细错误信息

2. **无法访问网页**
   - 确认设备 IP 地址：`10.10.99.97`
   - 检查网络连接
   - 确认防火墙没有阻止 80 端口

3. **页面显示不正常**
   - 检查 `/sdcard/web` 目录中的文件是否完整
   - 验证文件权限和内容

4. **API 返回错误**
   - 检查系统组件状态
   - 查看控制台日志获取详细信息

### 调试命令
可以通过控制台使用以下命令进行调试：
- `storage` - 检查存储状态
- `ethernet` - 检查以太网状态
- `status` - 查看系统整体状态

## 日志信息

Web Server 组件使用 `WEB_SERVER` 标签输出日志，包括：
- 初始化和启动状态
- 文件服务请求
- API 调用记录
- 错误和警告信息

通过设置日志级别可以控制输出详细程度：
```c
esp_log_level_set("WEB_SERVER", ESP_LOG_INFO);
```