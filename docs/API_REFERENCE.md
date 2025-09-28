# robOS API 参考文档

## 概述

本文档提供了robOS系统中所有已实现组件的详细API参考。所有API都经过单元测试验证，并在ESP32S3硬件上测试通过。

## 事件管理器 (Event Manager)

### 包含头文件
```c
#include "event_manager.h"
```

### 数据类型

#### 事件处理器类型
```c
typedef void (*esp_event_handler_t)(void* handler_args, 
                                   esp_event_base_t base, 
                                   int32_t id, 
                                   void* event_data);
```

### 核心函数

#### event_manager_init
```c
esp_err_t event_manager_init(void);
```
**功能**: 初始化事件管理器  
**返回值**: 
- `ESP_OK`: 初始化成功
- `ESP_ERR_NO_MEM`: 内存不足
- `ESP_ERR_INVALID_STATE`: 已经初始化

**示例**:
```c
esp_err_t ret = event_manager_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize event manager");
    return ret;
}
```

#### event_manager_deinit
```c
esp_err_t event_manager_deinit(void);
```
**功能**: 反初始化事件管理器，清理所有资源  
**返回值**: 
- `ESP_OK`: 反初始化成功
- `ESP_ERR_INVALID_STATE`: 未初始化

#### event_manager_post_event
```c
esp_err_t event_manager_post_event(esp_event_base_t event_base,
                                  int32_t event_id,
                                  void* event_data,
                                  size_t event_data_size);
```
**功能**: 发布一个事件到事件循环  
**参数**:
- `event_base`: 事件基础标识符
- `event_id`: 事件ID
- `event_data`: 事件数据指针（可为NULL）
- `event_data_size`: 事件数据大小

**返回值**: 
- `ESP_OK`: 事件发布成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_INVALID_STATE`: 未初始化

**示例**:
```c
int status = 42;
esp_err_t ret = event_manager_post_event(MY_EVENTS, 
                                        MY_EVENT_STATUS_CHANGED,
                                        &status, 
                                        sizeof(status));
```

#### event_manager_register_handler
```c
esp_err_t event_manager_register_handler(esp_event_base_t event_base,
                                        int32_t event_id,
                                        esp_event_handler_t event_handler,
                                        void* event_handler_arg);
```
**功能**: 注册事件处理器  
**参数**:
- `event_base`: 事件基础标识符
- `event_id`: 事件ID（ESP_EVENT_ANY_ID表示任意事件）
- `event_handler`: 事件处理函数
- `event_handler_arg`: 传递给处理函数的参数

**返回值**: 
- `ESP_OK`: 注册成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_INVALID_STATE`: 未初始化

**示例**:
```c
void my_event_handler(void* handler_args, esp_event_base_t base, 
                     int32_t id, void* event_data) {
    int* status = (int*)event_data;
    ESP_LOGI(TAG, "Received status: %d", *status);
}

esp_err_t ret = event_manager_register_handler(MY_EVENTS,
                                              MY_EVENT_STATUS_CHANGED,
                                              my_event_handler,
                                              NULL);
```

#### event_manager_unregister_handler
```c
esp_err_t event_manager_unregister_handler(esp_event_base_t event_base,
                                          int32_t event_id,
                                          esp_event_handler_t event_handler);
```
**功能**: 注销事件处理器  
**返回值**: 
- `ESP_OK`: 注销成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_INVALID_STATE`: 未初始化

#### event_manager_is_initialized
```c
bool event_manager_is_initialized(void);
```
**功能**: 检查事件管理器是否已初始化  
**返回值**: 
- `true`: 已初始化
- `false`: 未初始化

## 硬件抽象层 (Hardware HAL)

### 包含头文件
```c
#include "hardware_hal.h"
```

### 数据类型

#### GPIO配置结构
```c
typedef struct {
    gpio_num_t pin;              // GPIO引脚号
    gpio_mode_t mode;            // GPIO模式
    gpio_pullup_pulldown_t pull; // 上拉/下拉配置
    gpio_int_type_t intr_type;   // 中断类型
    bool invert;                 // 信号反转
} hal_gpio_config_t;
```

#### PWM配置结构
```c
typedef struct {
    ledc_channel_t channel;      // LEDC通道
    gpio_num_t gpio_num;         // GPIO引脚号
    uint32_t frequency;          // PWM频率(Hz)
    ledc_timer_bit_t duty_resolution; // 占空比分辨率
    uint32_t duty;              // 占空比值
} hal_pwm_config_t;
```

#### ADC配置结构
```c
typedef struct {
    adc_unit_t unit;            // ADC单元
    adc_channel_t channel;      // ADC通道
    adc_atten_t atten;          // 衰减设置
    adc_bitwidth_t bitwidth;    // 位宽
} hal_adc_config_t;
```

#### SPI配置结构
```c
typedef struct {
    spi_host_device_t host;     // SPI主机
    int mosi_io_num;            // MOSI引脚
    int miso_io_num;            // MISO引脚
    int sclk_io_num;            // SCLK引脚
    int cs_io_num;              // CS引脚
    int clock_speed_hz;         // 时钟频率
    int mode;                   // SPI模式
} hal_spi_config_t;
```

#### UART配置结构
```c
typedef struct {
    uart_port_t uart_num;       // UART端口
    int baud_rate;              // 波特率
    uart_word_length_t data_bits; // 数据位
    uart_parity_t parity;       // 奇偶校验
    uart_stop_bits_t stop_bits; // 停止位
    uart_hw_flowcontrol_t flow_ctrl; // 流控制
    int tx_pin;                 // TX引脚
    int rx_pin;                 // RX引脚
} hal_uart_config_t;
```

#### 状态结构
```c
typedef struct {
    uint32_t gpio_count;        // 已配置的GPIO数量
    uint32_t pwm_count;         // 已配置的PWM数量
    uint32_t spi_count;         // 已配置的SPI数量
    uint32_t adc_count;         // 已配置的ADC数量
    uint32_t uart_count;        // 已配置的UART数量
} hal_status_t;
```

### 核心管理函数

#### hardware_hal_init
```c
esp_err_t hardware_hal_init(void);
```
**功能**: 初始化硬件抽象层  
**返回值**: 
- `ESP_OK`: 初始化成功
- `ESP_ERR_NO_MEM`: 内存不足

#### hardware_hal_deinit
```c
esp_err_t hardware_hal_deinit(void);
```
**功能**: 反初始化硬件抽象层，清理所有资源  
**返回值**: 
- `ESP_OK`: 反初始化成功
- `ESP_ERR_INVALID_STATE`: 未初始化

#### hardware_hal_is_initialized
```c
bool hardware_hal_is_initialized(void);
```
**功能**: 检查硬件抽象层是否已初始化  
**返回值**: 
- `true`: 已初始化
- `false`: 未初始化

#### hardware_hal_get_status
```c
esp_err_t hardware_hal_get_status(hal_status_t *status);
```
**功能**: 获取硬件抽象层状态信息  
**参数**:
- `status`: 状态结构指针

**返回值**: 
- `ESP_OK`: 获取成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_INVALID_STATE`: 未初始化

### GPIO函数

#### hal_gpio_configure
```c
esp_err_t hal_gpio_configure(const hal_gpio_config_t *config);
```
**功能**: 配置GPIO引脚  
**参数**:
- `config`: GPIO配置结构指针

**返回值**: 
- `ESP_OK`: 配置成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_INVALID_STATE`: 未初始化

**示例**:
```c
hal_gpio_config_t gpio_cfg = {
    .pin = GPIO_NUM_2,
    .mode = GPIO_MODE_INPUT_OUTPUT,
    .pull = GPIO_FLOATING,
    .intr_type = GPIO_INTR_DISABLE,
    .invert = false
};
esp_err_t ret = hal_gpio_configure(&gpio_cfg);
```

#### hal_gpio_set_level
```c
esp_err_t hal_gpio_set_level(gpio_num_t pin, uint32_t level);
```
**功能**: 设置GPIO输出电平  
**参数**:
- `pin`: GPIO引脚号
- `level`: 电平值（0或1）

**返回值**: 
- `ESP_OK`: 设置成功
- `ESP_ERR_INVALID_ARG`: 参数无效

#### hal_gpio_get_level
```c
esp_err_t hal_gpio_get_level(gpio_num_t pin, uint32_t *level);
```
**功能**: 读取GPIO电平  
**参数**:
- `pin`: GPIO引脚号
- `level`: 电平值指针

**返回值**: 
- `ESP_OK`: 读取成功
- `ESP_ERR_INVALID_ARG`: 参数无效

#### hal_gpio_toggle
```c
esp_err_t hal_gpio_toggle(gpio_num_t pin);
```
**功能**: 翻转GPIO电平  
**参数**:
- `pin`: GPIO引脚号

**返回值**: 
- `ESP_OK`: 翻转成功
- `ESP_ERR_INVALID_ARG`: 参数无效

### PWM函数

#### hal_pwm_configure
```c
esp_err_t hal_pwm_configure(const hal_pwm_config_t *config);
```
**功能**: 配置PWM通道  
**参数**:
- `config`: PWM配置结构指针

**示例**:
```c
hal_pwm_config_t pwm_cfg = {
    .channel = LEDC_CHANNEL_0,
    .gpio_num = GPIO_NUM_4,
    .frequency = 1000,
    .duty_resolution = LEDC_TIMER_12_BIT,
    .duty = 2048  // 50% duty cycle
};
esp_err_t ret = hal_pwm_configure(&pwm_cfg);
```

#### hal_pwm_set_duty
```c
esp_err_t hal_pwm_set_duty(ledc_channel_t channel, uint32_t duty);
```
**功能**: 设置PWM占空比  
**参数**:
- `channel`: LEDC通道
- `duty`: 占空比值

#### hal_pwm_start
```c
esp_err_t hal_pwm_start(ledc_channel_t channel);
```
**功能**: 启动PWM输出  

#### hal_pwm_stop
```c
esp_err_t hal_pwm_stop(ledc_channel_t channel);
```
**功能**: 停止PWM输出  

### ADC函数

#### hal_adc_configure
```c
esp_err_t hal_adc_configure(const hal_adc_config_t *config);
```
**功能**: 配置ADC通道  
**参数**:
- `config`: ADC配置结构指针

**示例**:
```c
hal_adc_config_t adc_cfg = {
    .unit = ADC_UNIT_1,
    .channel = ADC_CHANNEL_0,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12
};
esp_err_t ret = hal_adc_configure(&adc_cfg);
```

#### hal_adc_read_voltage
```c
esp_err_t hal_adc_read_voltage(adc_channel_t channel, int *voltage_mv);
```
**功能**: 读取ADC电压值（经过校准）  
**参数**:
- `channel`: ADC通道
- `voltage_mv`: 电压值指针（mV）

**返回值**: 
- `ESP_OK`: 读取成功
- `ESP_ERR_INVALID_ARG`: 参数无效

### SPI函数

#### hal_spi_configure
```c
esp_err_t hal_spi_configure(const hal_spi_config_t *config);
```
**功能**: 配置SPI主机  
**参数**:
- `config`: SPI配置结构指针

#### hal_spi_transmit
```c
esp_err_t hal_spi_transmit(spi_host_device_t host, const uint8_t *data, size_t len);
```
**功能**: 通过SPI发送数据  
**参数**:
- `host`: SPI主机
- `data`: 数据缓冲区
- `len`: 数据长度

### UART函数

#### hal_uart_configure
```c
esp_err_t hal_uart_configure(const hal_uart_config_t *config);
```
**功能**: 配置UART端口  
**参数**:
- `config`: UART配置结构指针

#### hal_uart_write
```c
esp_err_t hal_uart_write(uart_port_t uart_num, const char* data, size_t len);
```
**功能**: 通过UART发送数据  
**参数**:
- `uart_num`: UART端口号
- `data`: 数据缓冲区
- `len`: 数据长度

#### hal_uart_read
```c
esp_err_t hal_uart_read(uart_port_t uart_num, uint8_t* data, size_t len, size_t* read_len);
```
**功能**: 从UART读取数据  
**参数**:
- `uart_num`: UART端口号
- `data`: 数据缓冲区
- `len`: 缓冲区大小
- `read_len`: 实际读取长度指针

## 常量定义

### 事件管理器常量
```c
ESP_EVENT_DECLARE_BASE(ROBOS_EVENTS);    // robOS事件基础
#define ROBOS_EVENT_SYSTEM_READY    (1)  // 系统就绪事件
#define ROBOS_EVENT_ERROR           (2)  // 错误事件
```

### 硬件抽象层常量
```c
#define HAL_GPIO_MAX_PIN    (48)    // 最大GPIO引脚数
#define HAL_SPI_MAX_HOST    (3)     // 最大SPI主机数
#define HAL_UART_MAX_NUM    (3)     // 最大UART数量
```

## 错误代码

所有函数使用标准ESP-IDF错误代码：

- `ESP_OK` (0): 操作成功
- `ESP_ERR_INVALID_ARG` (0x102): 参数无效
- `ESP_ERR_INVALID_STATE` (0x103): 状态无效
- `ESP_ERR_NO_MEM` (0x101): 内存不足
- `ESP_ERR_NOT_FOUND` (0x105): 未找到
- `ESP_ERR_TIMEOUT` (0x107): 超时

## 使用示例

### 完整的GPIO控制示例
```c
#include "hardware_hal.h"
#include "esp_log.h"

static const char* TAG = "GPIO_EXAMPLE";

void gpio_example(void) {
    // 初始化硬件抽象层
    esp_err_t ret = hardware_hal_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HAL");
        return;
    }
    
    // 配置GPIO
    hal_gpio_config_t gpio_cfg = {
        .pin = GPIO_NUM_2,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull = GPIO_FLOATING,
        .intr_type = GPIO_INTR_DISABLE,
        .invert = false
    };
    
    ret = hal_gpio_configure(&gpio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO");
        return;
    }
    
    // 控制GPIO
    hal_gpio_set_level(GPIO_NUM_2, 1);  // 设置高电平
    
    uint32_t level;
    hal_gpio_get_level(GPIO_NUM_2, &level);
    ESP_LOGI(TAG, "GPIO level: %lu", level);
    
    hal_gpio_toggle(GPIO_NUM_2);  // 翻转电平
    
    // 清理资源
    hardware_hal_deinit();
}
```

### 事件系统使用示例
```c
#include "event_manager.h"
#include "esp_log.h"

ESP_EVENT_DEFINE_BASE(MY_EVENTS);
static const char* TAG = "EVENT_EXAMPLE";

void my_event_handler(void* handler_args, esp_event_base_t base, 
                     int32_t id, void* event_data) {
    int* data = (int*)event_data;
    ESP_LOGI(TAG, "Received event with data: %d", *data);
}

void event_example(void) {
    // 初始化事件管理器
    esp_err_t ret = event_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize event manager");
        return;
    }
    
    // 注册事件处理器
    ret = event_manager_register_handler(MY_EVENTS, 1, my_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register handler");
        return;
    }
    
    // 发布事件
    int event_data = 42;
    ret = event_manager_post_event(MY_EVENTS, 1, &event_data, sizeof(event_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event");
        return;
    }
    
    // 清理资源
    event_manager_deinit();
}
```

## 控制台核心组件 (Console Core)

### 包含头文件
```c
#include "console_core.h"
```

### 数据类型

#### 命令函数类型
```c
typedef esp_err_t (*console_cmd_func_t)(int argc, char **argv);
```

#### 命令结构
```c
typedef struct {
    const char *command;        // 命令名称
    const char *help;          // 帮助文本
    const char *hint;          // 命令提示
    console_cmd_func_t func;   // 命令函数指针
    uint8_t min_args;          // 最小参数数量
    uint8_t max_args;          // 最大参数数量
} console_cmd_t;
```

#### 配置结构
```c
typedef struct {
    uart_port_t uart_port;     // UART端口号
    int baud_rate;             // 波特率
    int tx_pin;                // TX引脚
    int rx_pin;                // RX引脚
    const char *prompt;        // 提示符字符串
    bool echo_enabled;         // 启用字符回显
    bool history_enabled;      // 启用命令历史
    bool completion_enabled;   // 启用命令补全
} console_config_t;
```

#### 状态结构
```c
typedef struct {
    bool initialized;          // 初始化状态
    uint32_t commands_count;   // 已注册命令数量
    uint32_t history_count;    // 历史命令数量
    uint32_t total_commands;   // 总执行命令数
    uart_port_t uart_port;     // 当前UART端口
    int baud_rate;             // 当前波特率
} console_status_t;
```

### 核心管理函数

#### console_core_init
```c
esp_err_t console_core_init(const console_config_t *config);
```
**功能**: 初始化控制台核心组件  
**参数**:
- `config`: 控制台配置结构指针

**返回值**: 
- `ESP_OK`: 初始化成功
- `ESP_ERR_INVALID_ARG`: 配置参数无效
- `ESP_ERR_NO_MEM`: 内存不足

**示例**:
```c
console_config_t config = console_get_default_config();
config.baud_rate = 115200;
config.prompt = "robOS> ";
esp_err_t ret = console_core_init(&config);
```

#### console_core_deinit
```c
esp_err_t console_core_deinit(void);
```
**功能**: 反初始化控制台核心组件，清理所有资源  

#### console_core_start
```c
esp_err_t console_core_start(void);
```
**功能**: 启动控制台任务，开始处理输入输出  

#### console_core_stop
```c
esp_err_t console_core_stop(void);
```
**功能**: 停止控制台任务  

### 命令管理函数

#### console_register_command
```c
esp_err_t console_register_command(const console_cmd_t *cmd);
```
**功能**: 注册一个命令到控制台  
**参数**:
- `cmd`: 命令结构指针

**示例**:
```c
esp_err_t my_command_handler(int argc, char **argv) {
    console_printf("Hello from my command!\r\n");
    return ESP_OK;
}

console_cmd_t my_cmd = {
    .command = "hello",
    .help = "hello - Print greeting message",
    .func = my_command_handler,
    .min_args = 0,
    .max_args = 0
};

esp_err_t ret = console_register_command(&my_cmd);
```

#### console_unregister_command
```c
esp_err_t console_unregister_command(const char *command);
```
**功能**: 从控制台注销一个命令  

#### console_execute_command
```c
esp_err_t console_execute_command(const char *command_line);
```
**功能**: 直接执行一个命令字符串  
**参数**:
- `command_line`: 要执行的命令行字符串

### 输入输出函数

#### console_printf
```c
int console_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
```
**功能**: 格式化打印到控制台  
**返回值**: 打印的字符数

#### console_print
```c
esp_err_t console_print(const char *text);
```
**功能**: 打印文本到控制台  

#### console_println
```c
esp_err_t console_println(const char *text);
```
**功能**: 打印文本到控制台并换行  

#### console_readline
```c
esp_err_t console_readline(char *buffer, size_t buffer_size, uint32_t timeout_ms);
```
**功能**: 从控制台读取一行输入  
**参数**:
- `buffer`: 输入缓冲区
- `buffer_size`: 缓冲区大小
- `timeout_ms`: 超时时间(毫秒)

### 提示符和显示函数

#### console_set_prompt
```c
esp_err_t console_set_prompt(const char *prompt);
```
**功能**: 设置控制台提示符  

#### console_get_prompt
```c
const char* console_get_prompt(void);
```
**功能**: 获取当前提示符字符串  

#### console_clear
```c
esp_err_t console_clear(void);
```
**功能**: 清除控制台屏幕  

### 历史管理函数

#### console_get_history
```c
const char* console_get_history(uint32_t index);
```
**功能**: 获取历史命令  
**参数**:
- `index`: 历史索引(0为最新)

#### console_clear_history
```c
esp_err_t console_clear_history(void);
```
**功能**: 清除命令历史  

### 内置命令

控制台核心组件提供以下内置命令：

- **help** `[command]` - 显示所有可用命令或特定命令的帮助
- **version** - 显示系统版本信息
- **clear** - 清除控制台屏幕
- **history** - 显示命令历史记录
- **status** - 显示控制台状态信息

### 配置函数

#### console_get_default_config
```c
console_config_t console_get_default_config(void);
```
**功能**: 获取默认控制台配置  
**返回值**: 默认配置结构

**默认配置**:
```c
console_config_t config = {
    .uart_port = UART_NUM_0,
    .baud_rate = 115200,
    .tx_pin = UART_PIN_NO_CHANGE,
    .rx_pin = UART_PIN_NO_CHANGE,
    .prompt = "robOS> ",
    .echo_enabled = true,
    .history_enabled = true,
    .completion_enabled = true
};
```

### 使用示例

#### 完整的控制台应用示例
```c
#include "console_core.h"
#include "esp_log.h"

static const char* TAG = "CONSOLE_APP";

esp_err_t led_command(int argc, char **argv) {
    if (argc < 2) {
        console_println("Usage: led <on|off>");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strcmp(argv[1], "on") == 0) {
        console_println("LED turned ON");
        // TODO: 实际控制LED
    } else if (strcmp(argv[1], "off") == 0) {
        console_println("LED turned OFF");
        // TODO: 实际控制LED
    } else {
        console_println("Error: Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

void app_main(void) {
    // 初始化控制台
    console_config_t config = console_get_default_config();
    config.prompt = "MyApp> ";
    
    esp_err_t ret = console_core_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize console");
        return;
    }
    
    // 注册自定义命令
    console_cmd_t led_cmd = {
        .command = "led",
        .help = "led <on|off> - Control LED state",
        .func = led_command,
        .min_args = 1,
        .max_args = 1
    };
    
    ret = console_register_command(&led_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register LED command");
        return;
    }
    
    // 启动控制台
    ret = console_core_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start console");
        return;
    }
    
    ESP_LOGI(TAG, "Console started successfully");
    
    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

*文档版本: v1.1*  
*更新时间: 2025年9月28日*  
*适用版本: robOS v0.2.0*