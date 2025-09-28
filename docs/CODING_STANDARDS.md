# robOS 代码规范和架构指南

## 🎯 总体原则

### 设计哲学
- **组件至上**: 所有功能都必须封装为独立的ESP-IDF组件
- **接口标准化**: 所有组件都提供统一的API接口
- **事件驱动**: 组件间通过事件系统进行通信，避免直接依赖
- **测试驱动**: 每个组件都必须有对应的单元测试
- **文档先行**: API设计前必须先编写接口文档

## 📁 项目目录结构

```
robOS/
├── components/                    # ESP-IDF组件目录
│   ├── event_manager/            # 事件管理组件 (核心)
│   ├── hardware_hal/             # 硬件抽象层组件 (核心)
│   ├── console_core/             # 控制台核心组件 (核心)
│   ├── led_controller/           # LED控制组件
│   ├── ethernet_manager/         # 以太网管理组件
│   ├── storage_manager/          # 存储管理组件
│   ├── power_monitor/            # 电源监控组件
│   ├── device_manager/           # 设备管理组件
│   └── system_monitor/           # 系统监控组件
├── main/                         # 主应用程序
├── docs/                         # 项目文档
├── tests/                        # 单元测试
└── tools/                        # 开发工具
```

## 🏗️ 组件架构规范

### 组件层次结构
```
应用层 (main/)
    ↓
业务组件层 (led_controller, ethernet_manager, etc.)
    ↓
核心组件层 (console_core, event_manager)
    ↓
硬件抽象层 (hardware_hal)
    ↓
ESP-IDF/硬件层
```

### 组件接口规范
每个组件必须提供以下标准接口：

```c
// 组件初始化和清理
esp_err_t [component]_init(const [component]_config_t *config);
esp_err_t [component]_deinit(void);

// 组件启动和停止
esp_err_t [component]_start(void);
esp_err_t [component]_stop(void);

// 组件配置
esp_err_t [component]_configure(const [component]_config_t *config);
esp_err_t [component]_get_config([component]_config_t *config);

// 组件状态查询
esp_err_t [component]_get_status([component]_status_t *status);
bool [component]_is_initialized(void);
bool [component]_is_running(void);
```

## 💻 代码风格规范

### 命名约定
- **组件名**: 小写+下划线，如 `event_manager`
- **文件名**: 组件名.h/.c，如 `event_manager.h`
- **函数名**: 组件名_动作，如 `event_manager_init()`
- **结构体**: 组件名_类型_t，如 `event_manager_config_t`
- **枚举**: 组件名_枚举名_t，如 `event_manager_event_type_t`
- **宏定义**: 全大写+下划线，如 `EVENT_MANAGER_MAX_HANDLERS`

### 错误处理
- 所有公共API都必须返回 `esp_err_t`
- 使用ESP-IDF标准错误码
- 关键错误必须记录日志

### 日志规范
```c
static const char *TAG = "EVENT_MANAGER";
ESP_LOGI(TAG, "Event manager initialized successfully");
ESP_LOGW(TAG, "Warning: Maximum handlers reached");
ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
```

## 🔧 组件开发模板

### 头文件模板 (component_name.h)
```c
/**
 * @file component_name.h
 * @brief Component Name API
 * 
 * This component provides [功能描述]
 * 
 * Features:
 * - Feature 1
 * - Feature 2
 * 
 * @author robOS Team
 * @date 2025
 */

#pragma once

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Component Name Configuration
 */
typedef struct {
    // 配置参数
} component_name_config_t;

/**
 * @brief Component Name Status
 */
typedef struct {
    bool initialized;
    bool running;
    // 其他状态信息
} component_name_status_t;

/**
 * @brief Component Name Events
 */
typedef enum {
    COMPONENT_NAME_EVENT_STARTED,
    COMPONENT_NAME_EVENT_STOPPED,
    COMPONENT_NAME_EVENT_ERROR,
} component_name_event_type_t;

/**
 * @brief Initialize Component Name
 * @param config Configuration parameters
 * @return ESP_OK on success
 */
esp_err_t component_name_init(const component_name_config_t *config);

/**
 * @brief Deinitialize Component Name
 * @return ESP_OK on success
 */
esp_err_t component_name_deinit(void);

// 其他API声明...

#ifdef __cplusplus
}
#endif
```

## 🧪 测试驱动开发 (TDD)

### 测试结构
```
tests/
├── test_[component_name]/
│   ├── test_[component_name].c      # 单元测试
│   ├── test_[component_name]_integration.c  # 集成测试
│   └── CMakeLists.txt
└── CMakeLists.txt
```

### 测试命名约定
- 测试函数: `test_[component]_[function]_[scenario]()`
- 示例: `test_event_manager_register_handler_success()`
- 示例: `test_event_manager_register_handler_null_pointer()`

### 测试覆盖要求
- **单元测试**: 覆盖所有公共API
- **集成测试**: 覆盖组件间交互
- **边界测试**: 覆盖异常情况和边界条件

## 📝 文档规范

### API文档
- 所有公共函数都必须有详细的Doxygen注释
- 包含参数说明、返回值、使用示例

### 组件文档
每个组件都必须包含：
- README.md: 组件概述和快速开始
- API.md: 详细API文档
- EXAMPLES.md: 使用示例

## 🔄 事件系统规范

### 事件定义
```c
// 在各组件头文件中定义事件基础
ESP_EVENT_DECLARE_BASE(COMPONENT_NAME_EVENTS);

// 事件数据结构
typedef struct {
    // 事件相关数据
} component_name_event_data_t;
```

### 事件发布
```c
component_name_event_data_t event_data = {
    // 填充数据
};
esp_event_post(COMPONENT_NAME_EVENTS, 
               COMPONENT_NAME_EVENT_TYPE, 
               &event_data, 
               sizeof(event_data), 
               portMAX_DELAY);
```

## 🚀 开发流程

### 新组件开发流程
1. **设计阶段**: 编写API文档和接口定义
2. **测试阶段**: 编写单元测试用例
3. **实现阶段**: 实现组件功能
4. **集成阶段**: 编写集成测试
5. **文档阶段**: 完善使用文档和示例

### Git提交规范
- feat: 新功能
- fix: 修复
- docs: 文档
- test: 测试
- refactor: 重构
- chore: 构建/工具相关

示例: `feat(event_manager): add event handler registration`

## 📋 质量检查清单

### 代码提交前检查
- [ ] 代码符合命名约定
- [ ] 所有公共API都有文档注释
- [ ] 单元测试通过
- [ ] 内存泄漏检查通过
- [ ] 日志输出适当
- [ ] 错误处理完整

### 组件完成检查
- [ ] API文档完整
- [ ] 单元测试覆盖率 > 80%
- [ ] 集成测试通过
- [ ] 使用示例可运行
- [ ] 真机测试通过

---

这个规范将确保robOS项目的高质量和可维护性，为未来的扩展奠定坚实基础。