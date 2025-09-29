# Matrix LED 组件

Matrix LED 是一个用于控制 32x32 WS2812 LED 矩阵的完整解决方案，提供了丰富的显示功能和易用的 API 接口。

## 📋 功能特性

### 🎨 显示功能
- **32x32 像素矩阵**: 1024 个独立控制的 RGB LED
- **真彩色显示**: 24 位 RGB 颜色深度，支持 1600 万种颜色
- **实时刷新**: 高达 60+ FPS 的流畅显示效果
- **亮度控制**: 101 级亮度调节（0-100%）

### 🖌️ 绘图功能
- **像素级控制**: 单个像素精确设置和读取
- **几何图形**: 直线、矩形、圆形绘制
- **批量操作**: 高效的多像素同时设置
- **填充功能**: 纯色填充和清空操作

### 🎭 动画系统
- **预置动画**: 彩虹、波浪、呼吸、旋转、渐变等效果
- **自定义动画**: 支持从文件加载动画数据
- **动画控制**: 播放、暂停、停止、循环控制
- **可配置参数**: 速度、颜色、持续时间等

### 🎨 颜色工具
- **颜色空间转换**: RGB ↔ HSV 双向转换
- **颜色插值**: 平滑的颜色渐变计算
- **亮度应用**: 独立的亮度控制算法
- **预定义颜色**: 常用颜色常量

### ⚙️ 系统集成
- **事件驱动**: 完整的事件通知系统
- **配置持久化**: NVS 配置存储和恢复
- **控制台命令**: 丰富的命令行交互接口
- **多任务安全**: 线程安全的并发访问

## 🔧 硬件规格

| 参数 | 值 | 说明 |
|------|------|------|
| GPIO 引脚 | 9 | WS2812 数据线 |
| 矩阵尺寸 | 32×32 | 总共 1024 个 LED |
| LED 类型 | WS2812 | 可编程 RGB LED |
| 颜色格式 | GRB | 绿-红-蓝顺序 |
| 驱动方式 | RMT + DMA | 硬件驱动，CPU 占用低 |
| 时钟频率 | 10MHz | RMT 分辨率 |
| 最大功耗 | 约 60W | 全白最大亮度时 |

## 🚀 快速开始

### 1. 初始化组件

```c
#include "matrix_led.h"

// 初始化 Matrix LED 组件
esp_err_t ret = matrix_led_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize matrix LED: %s", esp_err_to_name(ret));
    return;
}
```

### 2. 基本像素操作

```c
// 设置单个像素为红色
matrix_led_color_t red = {255, 0, 0};
matrix_led_set_pixel(10, 15, red);

// 刷新显示
matrix_led_refresh();

// 清空所有像素
matrix_led_clear();
matrix_led_refresh();

// 填充整个矩阵为蓝色
matrix_led_color_t blue = {0, 0, 255};
matrix_led_fill(blue);
matrix_led_refresh();
```

### 3. 图形绘制

```c
// 绘制直线
matrix_led_draw_line(0, 0, 31, 31, MATRIX_LED_COLOR_GREEN);

// 绘制矩形边框
matrix_led_rect_t rect = {5, 5, 20, 15};
matrix_led_draw_rect(&rect, MATRIX_LED_COLOR_YELLOW, false);

// 绘制填充圆形
matrix_led_draw_circle(16, 16, 8, MATRIX_LED_COLOR_CYAN, true);

matrix_led_refresh();
```

### 4. 动画播放

```c
// 播放彩虹动画，速度 70%
matrix_led_rainbow_gradient(70);

// 播放呼吸效果
matrix_led_breathe_effect(MATRIX_LED_COLOR_PURPLE, 50);

// 停止当前动画
matrix_led_stop_animation();
```

### 5. 亮度和模式控制

```c
// 设置亮度为 30%
matrix_led_set_brightness(30);

// 切换到动画模式
matrix_led_set_mode(MATRIX_LED_MODE_ANIMATION);

// 禁用矩阵显示
matrix_led_set_enable(false);
```

## 📚 API 参考

### 初始化和状态管理

```c
esp_err_t matrix_led_init(void);
esp_err_t matrix_led_deinit(void);
esp_err_t matrix_led_set_enable(bool enable);
bool matrix_led_is_enabled(void);
esp_err_t matrix_led_get_status(matrix_led_status_t* status);
```

### 像素控制

```c
esp_err_t matrix_led_set_pixel(uint8_t x, uint8_t y, matrix_led_color_t color);
esp_err_t matrix_led_get_pixel(uint8_t x, uint8_t y, matrix_led_color_t* color);
esp_err_t matrix_led_set_pixels(const matrix_led_pixel_t* pixels, size_t count);
esp_err_t matrix_led_clear(void);
esp_err_t matrix_led_fill(matrix_led_color_t color);
esp_err_t matrix_led_refresh(void);
```

### 图形绘制

```c
esp_err_t matrix_led_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, matrix_led_color_t color);
esp_err_t matrix_led_draw_rect(const matrix_led_rect_t* rect, matrix_led_color_t color, bool filled);
esp_err_t matrix_led_draw_circle(uint8_t center_x, uint8_t center_y, uint8_t radius, matrix_led_color_t color, bool filled);
esp_err_t matrix_led_draw_text(uint8_t x, uint8_t y, const char* text, matrix_led_color_t color);
```

### 亮度和模式

```c
esp_err_t matrix_led_set_brightness(uint8_t brightness);
uint8_t matrix_led_get_brightness(void);
esp_err_t matrix_led_set_mode(matrix_led_mode_t mode);
matrix_led_mode_t matrix_led_get_mode(void);
```

### 动画控制

```c
esp_err_t matrix_led_play_animation(matrix_led_animation_type_t animation_type, const matrix_led_animation_config_t* config);
esp_err_t matrix_led_stop_animation(void);
esp_err_t matrix_led_rainbow_gradient(uint8_t speed);
esp_err_t matrix_led_breathe_effect(matrix_led_color_t color, uint8_t speed);
esp_err_t matrix_led_show_test_pattern(void);
```

### 颜色工具

```c
esp_err_t matrix_led_rgb_to_hsv(matrix_led_color_t rgb, matrix_led_hsv_t* hsv);
esp_err_t matrix_led_hsv_to_rgb(matrix_led_hsv_t hsv, matrix_led_color_t* rgb);
esp_err_t matrix_led_color_interpolate(matrix_led_color_t color1, matrix_led_color_t color2, float ratio, matrix_led_color_t* result);
esp_err_t matrix_led_apply_brightness(matrix_led_color_t color, uint8_t brightness, matrix_led_color_t* result);
```

### 配置管理

```c
esp_err_t matrix_led_save_config(void);
esp_err_t matrix_led_load_config(void);
esp_err_t matrix_led_reset_config(void);
```

## 🎮 控制台命令

Matrix LED 组件提供了丰富的控制台命令进行交互式控制：

### 基本状态和控制

```bash
# 显示矩阵状态
led matrix status

# 启用/禁用矩阵
led matrix enable on|off

# 设置亮度 (0-100)
led matrix brightness 50
```

### 像素和绘图操作

```bash
# 清空矩阵
led matrix clear

# 填充颜色 (RGB: 0-255)
led matrix fill 255 0 0

# 设置单个像素
led matrix pixel 10 15 0 255 0

# 显示测试图案
led matrix test
```

### 图形绘制

```bash
# 绘制直线
led matrix draw line 0 0 31 31 255 255 255

# 绘制矩形 (可选 fill 参数填充)
led matrix draw rect 5 5 10 8 255 0 255 fill

# 绘制圆形 (可选 fill 参数填充)
led matrix draw circle 16 16 8 0 255 255 fill
```

### 模式和动画

```bash
# 设置显示模式
led matrix mode static|animation|off

# 播放动画 (可选速度参数 0-100)
led matrix animation rainbow 70
led matrix animation wave 50
led matrix animation breathe 60
led matrix animation rotate 40
led matrix animation fade 30

# 停止动画
led matrix stop
```

### 配置管理

```bash
# 保存当前配置
led matrix config save

# 加载保存的配置
led matrix config load

# 重置为默认配置
led matrix config reset
```

## 🔧 配置参数

### 编译时配置

在 `matrix_led.h` 中定义的常量：

```c
#define MATRIX_LED_WIDTH         32       // 矩阵宽度
#define MATRIX_LED_HEIGHT        32       // 矩阵高度
#define MATRIX_LED_GPIO          9        // GPIO 引脚
#define MATRIX_LED_DEFAULT_BRIGHTNESS 50  // 默认亮度
```

### 运行时配置

通过 NVS 存储的配置项：

- `brightness`: 亮度设置 (0-100)
- `mode`: 显示模式
- `enable`: 启用状态
- `animation`: 当前动画名称

## 🎨 颜色常量

组件提供了预定义的颜色常量：

```c
MATRIX_LED_COLOR_BLACK     // 黑色 (0,0,0)
MATRIX_LED_COLOR_WHITE     // 白色 (255,255,255)
MATRIX_LED_COLOR_RED       // 红色 (255,0,0)
MATRIX_LED_COLOR_GREEN     // 绿色 (0,255,0)
MATRIX_LED_COLOR_BLUE      // 蓝色 (0,0,255)
MATRIX_LED_COLOR_YELLOW    // 黄色 (255,255,0)
MATRIX_LED_COLOR_CYAN      // 青色 (0,255,255)
MATRIX_LED_COLOR_MAGENTA   // 品红 (255,0,255)
MATRIX_LED_COLOR_ORANGE    // 橙色 (255,165,0)
MATRIX_LED_COLOR_PURPLE    // 紫色 (128,0,128)
```

## 📊 事件系统

Matrix LED 组件支持事件通知，可以监听以下事件：

- `MATRIX_LED_EVENT_INITIALIZED`: 初始化完成
- `MATRIX_LED_EVENT_MODE_CHANGED`: 模式变更
- `MATRIX_LED_EVENT_BRIGHTNESS_CHANGED`: 亮度变更
- `MATRIX_LED_EVENT_ANIMATION_STARTED`: 动画开始
- `MATRIX_LED_EVENT_ANIMATION_STOPPED`: 动画停止
- `MATRIX_LED_EVENT_ANIMATION_COMPLETED`: 动画完成
- `MATRIX_LED_EVENT_ERROR`: 错误事件

## 🔍 使用场景

### 状态指示

```c
// 系统启动指示
matrix_led_fill(MATRIX_LED_COLOR_GREEN);
matrix_led_set_brightness(30);
matrix_led_refresh();

// 错误指示
matrix_led_breathe_effect(MATRIX_LED_COLOR_RED, 80);
```

### 信息显示

```c
// 显示简单图标或符号
matrix_led_clear();
matrix_led_draw_circle(16, 16, 8, MATRIX_LED_COLOR_BLUE, true);
matrix_led_draw_circle(16, 16, 4, MATRIX_LED_COLOR_WHITE, true);
matrix_led_refresh();
```

### 环境指示

```c
// 根据温度显示不同颜色
if (temperature > 30) {
    matrix_led_fill(MATRIX_LED_COLOR_RED);
} else if (temperature > 20) {
    matrix_led_fill(MATRIX_LED_COLOR_YELLOW);
} else {
    matrix_led_fill(MATRIX_LED_COLOR_BLUE);
}
matrix_led_refresh();
```

### 装饰效果

```c
// 彩虹循环效果
matrix_led_rainbow_gradient(60);

// 呼吸灯效果
matrix_led_breathe_effect(MATRIX_LED_COLOR_CYAN, 40);
```

## ⚠️ 注意事项

### 硬件要求

1. **电源供应**: 32x32 矩阵在全白最大亮度时功耗可达 60W，请确保电源充足
2. **信号完整性**: GPIO 9 到 LED 矩阵的连线应尽量短，避免信号干扰
3. **散热考虑**: 长时间高亮度运行时注意散热

### 软件限制

1. **内存使用**: 组件会分配约 3KB 的像素缓冲区内存
2. **并发访问**: 多个任务同时访问时会有互斥锁保护，可能产生阻塞
3. **刷新频率**: 过高的刷新频率可能影响系统性能

### 最佳实践

1. **亮度设置**: 室内使用建议亮度不超过 50%，避免过亮刺眼
2. **动画优化**: 复杂动画建议降低帧率以节省 CPU 资源
3. **配置管理**: 重要设置及时保存到 NVS，防止断电丢失

## 🧪 测试

组件包含完整的单元测试，覆盖所有主要功能：

```bash
# 运行测试
idf.py build
idf.py flash monitor -p /dev/ttyUSB0
```

测试项目包括：
- 初始化和反初始化
- 像素设置和读取
- 图形绘制功能
- 动画播放控制
- 颜色工具函数
- 配置管理
- 错误处理
- 性能测试

## 🐛 故障排除

### 常见问题

**Q: LED 矩阵不亮**
A: 检查电源供应、GPIO 连接和初始化状态

**Q: 颜色显示不正确**
A: 确认 LED 类型为 WS2812 且使用 GRB 颜色格式

**Q: 动画播放卡顿**
A: 降低动画帧率或减少其他任务的 CPU 占用

**Q: 配置丢失**
A: 确保调用 `matrix_led_save_config()` 保存配置

### 调试信息

启用调试日志：

```c
esp_log_level_set("matrix_led", ESP_LOG_DEBUG);
```

## 📄 许可证

本组件基于 MIT 许可证开源。

## 🤝 贡献

欢迎提交 Issue 和 Pull Request 来改进这个组件！

---

**Matrix LED 组件** - 让您的 LED 矩阵显示更精彩！ ✨