# robOS 技术架构文档

## 系统架构概述

robOS 采用分层的组件化架构，旨在构建一个高度模块化、可扩展的机器人操作系统。系统基于ESP32S3平台，使用ESP-IDF框架开发。

## 架构层次

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Application Layer)                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │   Robot Apps    │  │   User Scripts  │  │   Control Logic │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    服务层 (Service Layer)                     │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │  LED Controller │  │ System Monitor  │  │  Device Manager │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │Ethernet Manager │  │Storage Manager  │  │  Power Monitor  │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    核心层 (Core Layer)                        │
│  ┌─────────────────┐  ┌─────────────────┐                     │
│  │  Console Core   │  │ Event Manager   │ ✅                   │
│  └─────────────────┘  └─────────────────┘                     │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                 硬件抽象层 (HAL Layer)                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              Hardware HAL ✅                             │ │
│  │  GPIO │ PWM │ SPI │ ADC │ UART │ I2C │ ...              │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                  硬件层 (Hardware Layer)                      │
│              ESP32S3 + Peripherals                          │
└─────────────────────────────────────────────────────────────┘
```

## 核心组件详解

### 1. 事件管理器 (Event Manager) ✅

**设计目标**: 提供统一的事件发布/订阅机制，实现组件间的松耦合通信。

**架构特点**:
- 基于ESP-IDF事件系统的高级封装
- 线程安全的并发访问控制
- 支持多事件处理器注册
- 统一的错误处理和状态管理

**核心数据结构**:
```c
typedef struct {
    bool initialized;                    // 初始化状态
    SemaphoreHandle_t mutex;            // 互斥锁
    esp_event_loop_handle_t event_loop; // 事件循环句柄
    uint32_t handler_count;             // 处理器计数
} event_manager_context_t;
```

**事件流程**:
```
事件发布者 → event_manager_post_event() → ESP事件系统 → 事件处理器 → 业务逻辑
```

### 2. 硬件抽象层 (Hardware HAL) ✅

**设计目标**: 为上层组件提供统一的硬件访问接口，屏蔽底层硬件差异。

**架构特点**:
- 统一的API接口设计
- 完整的资源管理和状态跟踪
- 支持多种外设的并发访问
- 标准化的配置和错误处理

### 3. 控制台核心组件 (Console Core) ✅

**设计目标**: 提供完整的命令行交互接口，实现人机交互和系统管理功能。

**架构特点**:
- 基于UART的命令行界面
- 模块化的命令注册和管理系统
- 支持命令历史和自动补全
- 线程安全的并发处理机制
- 可扩展的内置命令系统

**核心数据结构**:
```c
typedef struct {
    bool initialized;                               // 初始化状态
    SemaphoreHandle_t mutex;                       // 互斥锁
    hal_status_t status;                           // 状态信息
    bool gpio_pins_configured[HAL_GPIO_MAX_PIN];   // GPIO配置状态
    bool spi_hosts_configured[HAL_SPI_MAX_HOST];   // SPI主机配置状态
    spi_device_handle_t spi_devices[HAL_SPI_MAX_HOST]; // SPI设备句柄
    adc_oneshot_unit_handle_t adc_handles[SOC_ADC_PERIPH_NUM]; // ADC句柄
    adc_cali_handle_t adc_cali_handles[SOC_ADC_PERIPH_NUM];    // ADC校准句柄
} hal_context_t;
```

**硬件抽象层次**:
```
应用API → HAL统一接口 → ESP-IDF驱动 → 硬件寄存器
```

**核心数据结构**:
```c
typedef struct {
    bool initialized;                    // 初始化状态
    bool running;                        // 任务运行状态
    SemaphoreHandle_t mutex;            // 互斥锁
    TaskHandle_t task_handle;           // 控制台任务句柄
    QueueHandle_t input_queue;          // 输入字符队列
    console_config_t config;            // 控制台配置
    console_cmd_t commands[CONSOLE_MAX_COMMANDS]; // 命令注册表
    char history[CONSOLE_HISTORY_SIZE][CONSOLE_MAX_COMMAND_LENGTH]; // 历史缓冲区
    uint32_t command_count;             // 已注册命令数量
    uint32_t total_commands;            // 总执行命令数
} console_context_t;
```

**命令处理流程**:
```
UART输入 → 字符处理 → 命令解析 → 命令查找 → 参数验证 → 命令执行 → 结果输出
```

## 组件通信模式

### 1. 事件驱动通信
组件间通过事件管理器进行异步通信，实现松耦合架构：

```c
// 发布事件
event_manager_post_event(SYSTEM_EVENTS, SYSTEM_STATUS_CHANGED, &status, sizeof(status));

// 订阅事件
event_manager_register_handler(SYSTEM_EVENTS, SYSTEM_STATUS_CHANGED, status_handler, NULL);
```

### 2. 直接API调用
对于性能敏感的操作，支持直接API调用：

```c
// 直接硬件操作
hal_gpio_set_level(GPIO_NUM_2, 1);
hal_pwm_set_duty(LEDC_CHANNEL_0, 2048);
```

## 内存管理策略

### 1. 静态内存分配
- 核心组件使用静态全局变量
- 避免动态内存分配的不确定性
- 提高系统稳定性和可预测性

### 2. 资源池管理
- 预分配固定数量的资源句柄
- 统一的资源获取和释放机制
- 防止资源泄漏

### 3. 生命周期管理
```c
// 标准化的组件生命周期
esp_err_t component_init(void);    // 初始化
esp_err_t component_deinit(void);  // 清理
bool component_is_initialized(void); // 状态查询
```

## 错误处理策略

### 1. 分层错误处理
- 硬件层：ESP-IDF错误代码
- HAL层：统一错误映射和处理
- 应用层：业务逻辑错误处理

### 2. 错误传播机制
```c
esp_err_t ret = hal_gpio_configure(&config);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GPIO configuration failed: %s", esp_err_to_name(ret));
    return ret;
}
```

### 3. 状态恢复机制
- 组件级别的状态检查和恢复
- 自动重试机制
- 失败后的资源清理

## 并发控制

### 1. 互斥锁保护
每个组件使用专用互斥锁保护共享资源：

```c
if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // 临界区操作
    xSemaphoreGive(s_hal_ctx.mutex);
}
```

### 2. 原子操作
对于简单的状态查询使用原子操作：

```c
bool hardware_hal_is_initialized(void) {
    return s_hal_ctx.initialized;
}
```

## 性能优化

### 1. 零拷贝数据传输
- 直接传递数据指针而非拷贝数据
- 减少内存占用和CPU开销

### 2. 批处理操作
- 支持批量GPIO配置
- 减少系统调用开销

### 3. 中断驱动IO
- 异步数据处理
- 提高系统响应性

## 测试策略

### 1. 单元测试
- 每个组件独立测试
- Unity测试框架
- 100%代码覆盖率

### 2. 集成测试
- 组件间交互测试
- 真实硬件验证
- 性能基准测试

### 3. 压力测试
- 并发访问测试
- 长时间运行稳定性测试
- 资源泄漏检测

## 可扩展性设计

### 1. 插件架构
- 标准化的组件接口
- 动态组件加载机制
- 配置驱动的功能启用

### 2. 硬件抽象
- 统一的硬件接口
- 易于移植到其他平台
- 支持多种硬件配置

### 3. 模块化设计
- 独立的组件边界
- 最小化组件依赖
- 支持按需编译

## 开发工具链

### 1. 构建系统
- CMake构建系统
- 组件化编译
- 依赖管理

### 2. 调试工具
- ESP-IDF Monitor
- GDB调试支持
- 日志分级系统

### 3. 代码质量
- 统一代码风格
- 静态代码分析
- 自动化测试

## 未来架构演进

### 1. 微服务架构
- 组件服务化
- 独立的内存空间
- 进程间通信

### 2. 实时性增强
- 确定性任务调度
- 实时性能保证
- 优先级继承

### 3. 安全性加固
- 代码签名验证
- 安全启动
- 运行时保护

---

*文档版本: v1.1*  
*更新时间: 2025年9月28日*  
*状态: 开发阶段 - 3个核心组件已完成*