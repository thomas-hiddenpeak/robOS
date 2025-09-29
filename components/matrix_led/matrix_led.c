/**
 * @file matrix_led.c
 * @brief Matrix LED 控制组件实现 - 32x32 WS2812 LED矩阵控制
 */

#include "matrix_led.h"
#include "hardware_hal.h"
#include "config_manager.h"
#include "event_manager.h"
#include "console_core.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "led_strip.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <errno.h>
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

// ==================== 事件定义 ====================

ESP_EVENT_DEFINE_BASE(MATRIX_LED_EVENTS);

// ==================== 常量和宏定义 ====================

static const char *TAG = "matrix_led";

#define MATRIX_LED_TASK_STACK_SIZE      4096
#define MATRIX_LED_TASK_PRIORITY        3
#define MATRIX_LED_ANIMATION_TASK_DELAY_MS  20

#define MATRIX_LED_CONFIG_NAMESPACE     "matrix_led"
#define MATRIX_LED_CONFIG_KEY_BRIGHTNESS "brightness"
#define MATRIX_LED_CONFIG_KEY_MODE      "mode"
#define MATRIX_LED_CONFIG_KEY_ENABLE    "enable"
#define MATRIX_LED_CONFIG_KEY_ANIMATION "animation"
#define MATRIX_LED_CONFIG_KEY_STATIC_DATA "static_data"

// 动画文件默认路径
#define MATRIX_LED_ANIMATION_FILE_PATH  "/sdcard/matrix_animations.json"

// ==================== 预定义颜色常量 ====================

const matrix_led_color_t MATRIX_LED_COLOR_BLACK   = {0, 0, 0};
const matrix_led_color_t MATRIX_LED_COLOR_WHITE   = {255, 255, 255};
const matrix_led_color_t MATRIX_LED_COLOR_RED     = {255, 0, 0};
const matrix_led_color_t MATRIX_LED_COLOR_GREEN   = {0, 255, 0};
const matrix_led_color_t MATRIX_LED_COLOR_BLUE    = {0, 0, 255};
const matrix_led_color_t MATRIX_LED_COLOR_YELLOW  = {255, 255, 0};
const matrix_led_color_t MATRIX_LED_COLOR_CYAN    = {0, 255, 255};
const matrix_led_color_t MATRIX_LED_COLOR_MAGENTA = {255, 0, 255};
const matrix_led_color_t MATRIX_LED_COLOR_ORANGE  = {255, 165, 0};
const matrix_led_color_t MATRIX_LED_COLOR_PURPLE  = {128, 0, 128};

// ==================== 内部结构体和变量 ====================

/**
 * @brief 动画状态结构体
 */
typedef struct {
    bool is_running;                              ///< 动画是否在运行
    matrix_led_animation_type_t type;             ///< 当前动画类型
    matrix_led_animation_config_t config;        ///< 动画配置
    uint32_t frame_counter;                       ///< 帧计数器
    uint32_t start_time;                          ///< 开始时间
    uint8_t* custom_frames;                       ///< 自定义动画帧数据
    size_t custom_frame_count;                    ///< 自定义动画帧数量
    size_t current_frame;                         ///< 当前帧索引
} matrix_led_animation_state_t;

/**
 * @brief Matrix LED 主控制结构体
 */
typedef struct {
    // 基本状态
    bool initialized;                             ///< 初始化状态
    bool enabled;                                 ///< 启用状态
    matrix_led_mode_t mode;                       ///< 显示模式
    uint8_t brightness;                           ///< 亮度设置
    
    // LED硬件
    led_strip_handle_t led_strip;                 ///< LED条带句柄
    matrix_led_color_t* pixel_buffer;             ///< 像素缓冲区
    
    // 动画管理
    matrix_led_animation_state_t animation;       ///< 动画状态
    TaskHandle_t animation_task_handle;           ///< 动画任务句柄
    TimerHandle_t animation_timer;                ///< 动画定时器
    
    // 同步控制
    SemaphoreHandle_t mutex;                      ///< 互斥锁
    SemaphoreHandle_t refresh_semaphore;          ///< 刷新信号量
    SemaphoreHandle_t animation_semaphore;        ///< 动画信号量
    
    // 统计信息
    uint32_t frame_count;                         ///< 总帧数计数
    uint32_t last_refresh_time;                   ///< 上次刷新时间
} matrix_led_context_t;

// 全局上下文
static matrix_led_context_t s_context = {0};

// ==================== 静态函数声明 ====================

static esp_err_t matrix_led_init_hardware(void);
static esp_err_t matrix_led_deinit_hardware(void);
static void matrix_led_animation_task(void* pvParameters);
static void matrix_led_animation_timer_callback(TimerHandle_t xTimer);
static esp_err_t matrix_led_load_default_config(void);
static esp_err_t matrix_led_validate_coordinates(uint8_t x, uint8_t y);
static uint32_t matrix_led_xy_to_index(uint8_t x, uint8_t y);
static void matrix_led_index_to_xy(uint32_t index, uint8_t* x, uint8_t* y);
static esp_err_t matrix_led_send_event(matrix_led_event_type_t type, const matrix_led_event_data_t* data);

// 动画函数
static void matrix_led_animate_rainbow(void);
static void matrix_led_animate_wave(void);
static void matrix_led_animate_breathe(void);
static void matrix_led_animate_rotate(void);
static void matrix_led_animate_fade(void);

// 图形绘制辅助函数
static void matrix_led_draw_pixel_safe(uint8_t x, uint8_t y, matrix_led_color_t color);
static void matrix_led_draw_horizontal_line(uint8_t x0, uint8_t x1, uint8_t y, matrix_led_color_t color);
static void matrix_led_draw_vertical_line(uint8_t x, uint8_t y0, uint8_t y1, matrix_led_color_t color);

// 控制台命令函数
static void matrix_led_register_console_commands(void);

// ==================== 核心API实现 ====================

esp_err_t matrix_led_init(void)
{
    if (s_context.initialized) {
        ESP_LOGW(TAG, "Matrix LED already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing Matrix LED component...");

    // 清零上下文
    memset(&s_context, 0, sizeof(matrix_led_context_t));

    // 创建互斥锁
    s_context.mutex = xSemaphoreCreateMutex();
    if (s_context.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 创建刷新信号量
    s_context.refresh_semaphore = xSemaphoreCreateBinary();
    if (s_context.refresh_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create refresh semaphore");
        vSemaphoreDelete(s_context.mutex);
        return ESP_ERR_NO_MEM;
    }

    // 创建动画信号量
    s_context.animation_semaphore = xSemaphoreCreateBinary();
    if (s_context.animation_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create animation semaphore");
        vSemaphoreDelete(s_context.mutex);
        vSemaphoreDelete(s_context.refresh_semaphore);
        return ESP_ERR_NO_MEM;
    }

    // 分配像素缓冲区
    s_context.pixel_buffer = calloc(MATRIX_LED_COUNT, sizeof(matrix_led_color_t));
    if (s_context.pixel_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer");
        vSemaphoreDelete(s_context.mutex);
        vSemaphoreDelete(s_context.refresh_semaphore);
        vSemaphoreDelete(s_context.animation_semaphore);
        return ESP_ERR_NO_MEM;
    }

    // 初始化硬件
    esp_err_t ret = matrix_led_init_hardware();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hardware: %s", esp_err_to_name(ret));
        free(s_context.pixel_buffer);
        vSemaphoreDelete(s_context.mutex);
        vSemaphoreDelete(s_context.refresh_semaphore);
        vSemaphoreDelete(s_context.animation_semaphore);
        return ret;
    }

    // 设置默认值
    s_context.brightness = MATRIX_LED_DEFAULT_BRIGHTNESS;
    s_context.mode = MATRIX_LED_MODE_STATIC;
    s_context.enabled = true;

    // 创建动画定时器
    s_context.animation_timer = xTimerCreate(
        "matrix_led_timer",
        pdMS_TO_TICKS(MATRIX_LED_ANIMATION_TASK_DELAY_MS),
        pdTRUE,  // 自动重载
        NULL,
        matrix_led_animation_timer_callback
    );

    if (s_context.animation_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create animation timer");
        matrix_led_deinit_hardware();
        free(s_context.pixel_buffer);
        vSemaphoreDelete(s_context.mutex);
        vSemaphoreDelete(s_context.refresh_semaphore);
        vSemaphoreDelete(s_context.animation_semaphore);
        return ESP_ERR_NO_MEM;
    }

    // 创建动画任务
    BaseType_t task_ret = xTaskCreate(
        matrix_led_animation_task,
        "matrix_led_anim",
        MATRIX_LED_TASK_STACK_SIZE,
        NULL,
        MATRIX_LED_TASK_PRIORITY,
        &s_context.animation_task_handle
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create animation task");
        xTimerDelete(s_context.animation_timer, 0);
        matrix_led_deinit_hardware();
        free(s_context.pixel_buffer);
        vSemaphoreDelete(s_context.mutex);
        vSemaphoreDelete(s_context.refresh_semaphore);
        vSemaphoreDelete(s_context.animation_semaphore);
        return ESP_ERR_NO_MEM;
    }

    // 注册控制台命令
    matrix_led_register_console_commands();

    s_context.initialized = true;
    
    // 初始化完成后加载配置
    matrix_led_load_config();
    
    // 注意：不在这里执行清屏操作
    // 配置加载函数会处理动画恢复或静态内容恢复
    // 如果都没有恢复内容，配置加载函数会执行清屏

    // 发送初始化完成事件
    matrix_led_event_data_t event_data = {
        .type = MATRIX_LED_EVENT_INITIALIZED
    };
    matrix_led_send_event(MATRIX_LED_EVENT_INITIALIZED, &event_data);

    ESP_LOGI(TAG, "Matrix LED initialized successfully (GPIO: %d, Size: %dx%d, LEDs: %d)", 
             MATRIX_LED_GPIO, MATRIX_LED_WIDTH, MATRIX_LED_HEIGHT, MATRIX_LED_COUNT);

    return ESP_OK;
}

esp_err_t matrix_led_deinit(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing Matrix LED component...");

    // 停止动画
    matrix_led_stop_animation();

    // 删除任务和定时器
    if (s_context.animation_task_handle) {
        vTaskDelete(s_context.animation_task_handle);
        s_context.animation_task_handle = NULL;
    }

    if (s_context.animation_timer) {
        xTimerDelete(s_context.animation_timer, 0);
        s_context.animation_timer = NULL;
    }

    // 清空显示
    matrix_led_clear();
    matrix_led_refresh();

    // 反初始化硬件
    matrix_led_deinit_hardware();

    // 释放资源
    if (s_context.pixel_buffer) {
        free(s_context.pixel_buffer);
        s_context.pixel_buffer = NULL;
    }

    if (s_context.animation.custom_frames) {
        free(s_context.animation.custom_frames);
        s_context.animation.custom_frames = NULL;
    }

    if (s_context.mutex) {
        vSemaphoreDelete(s_context.mutex);
        s_context.mutex = NULL;
    }

    if (s_context.refresh_semaphore) {
        vSemaphoreDelete(s_context.refresh_semaphore);
        s_context.refresh_semaphore = NULL;
    }

    if (s_context.animation_semaphore) {
        vSemaphoreDelete(s_context.animation_semaphore);
        s_context.animation_semaphore = NULL;
    }

    s_context.initialized = false;

    ESP_LOGI(TAG, "Matrix LED deinitialized successfully");

    return ESP_OK;
}

bool matrix_led_is_initialized(void)
{
    return s_context.initialized;
}

esp_err_t matrix_led_set_enable(bool enable)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_context.enabled != enable) {
        s_context.enabled = enable;
        
        if (!enable) {
            // 禁用时清空显示
            matrix_led_clear();
            matrix_led_refresh();
            // 停止动画
            matrix_led_stop_animation();
        }

        ESP_LOGI(TAG, "Matrix LED %s", enable ? "enabled" : "disabled");
    }

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

bool matrix_led_is_enabled(void)
{
    return s_context.initialized && s_context.enabled;
}

esp_err_t matrix_led_get_status(matrix_led_status_t* status)
{
    if (!s_context.initialized || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    status->initialized = s_context.initialized;
    status->enabled = s_context.enabled;
    status->mode = s_context.mode;
    status->brightness = s_context.brightness;
    status->pixel_count = MATRIX_LED_COUNT;
    status->frame_count = s_context.frame_count;
    
    if (s_context.animation.is_running) {
        strncpy(status->current_animation, s_context.animation.config.name, 
                MATRIX_LED_MAX_NAME_LEN - 1);
        status->current_animation[MATRIX_LED_MAX_NAME_LEN - 1] = '\0';
    } else {
        status->current_animation[0] = '\0';
    }

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

// ==================== 像素控制API实现 ====================

esp_err_t matrix_led_set_pixel(uint8_t x, uint8_t y, matrix_led_color_t color)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = matrix_led_validate_coordinates(x, y);
    if (ret != ESP_OK) {
        return ret;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t index = matrix_led_xy_to_index(x, y);
    s_context.pixel_buffer[index] = color;

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

esp_err_t matrix_led_get_pixel(uint8_t x, uint8_t y, matrix_led_color_t* color)
{
    if (!s_context.initialized || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = matrix_led_validate_coordinates(x, y);
    if (ret != ESP_OK) {
        return ret;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t index = matrix_led_xy_to_index(x, y);
    *color = s_context.pixel_buffer[index];

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

esp_err_t matrix_led_set_pixels(const matrix_led_pixel_t* pixels, size_t count)
{
    if (!s_context.initialized || pixels == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = matrix_led_validate_coordinates(pixels[i].x, pixels[i].y);
        if (ret == ESP_OK) {
            uint32_t index = matrix_led_xy_to_index(pixels[i].x, pixels[i].y);
            s_context.pixel_buffer[index] = pixels[i].color;
        }
    }

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

esp_err_t matrix_led_clear(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(s_context.pixel_buffer, 0, MATRIX_LED_COUNT * sizeof(matrix_led_color_t));

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

esp_err_t matrix_led_fill(matrix_led_color_t color)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (uint32_t i = 0; i < MATRIX_LED_COUNT; i++) {
        s_context.pixel_buffer[i] = color;
    }

    xSemaphoreGive(s_context.mutex);
    return ESP_OK;
}

esp_err_t matrix_led_refresh(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_context.enabled) {
        return ESP_OK;  // 已禁用时不刷新
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // 应用亮度并发送到LED
    for (uint32_t i = 0; i < MATRIX_LED_COUNT; i++) {
        matrix_led_color_t adjusted_color;
        matrix_led_apply_brightness(s_context.pixel_buffer[i], s_context.brightness, &adjusted_color);
        
        esp_err_t ret = led_strip_set_pixel(s_context.led_strip, i, 
                                           adjusted_color.r, adjusted_color.g, adjusted_color.b);
        if (ret != ESP_OK) {
            xSemaphoreGive(s_context.mutex);
            return ret;
        }
    }

    esp_err_t ret = led_strip_refresh(s_context.led_strip);
    if (ret == ESP_OK) {
        s_context.frame_count++;
        s_context.last_refresh_time = xTaskGetTickCount();
    }

    xSemaphoreGive(s_context.mutex);
    return ret;
}

// ==================== 图形绘制API实现 ====================

esp_err_t matrix_led_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, matrix_led_color_t color)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Bresenham's line algorithm
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int x = x0, y = y0;

    while (true) {
        matrix_led_draw_pixel_safe(x, y, color);
        
        if (x == x1 && y == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }

    return ESP_OK;
}

esp_err_t matrix_led_draw_rect(const matrix_led_rect_t* rect, matrix_led_color_t color, bool filled)
{
    if (!s_context.initialized || rect == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (filled) {
        // 填充矩形
        for (uint8_t y = rect->y; y < rect->y + rect->height && y < MATRIX_LED_HEIGHT; y++) {
            for (uint8_t x = rect->x; x < rect->x + rect->width && x < MATRIX_LED_WIDTH; x++) {
                matrix_led_draw_pixel_safe(x, y, color);
            }
        }
    } else {
        // 绘制矩形边框
        matrix_led_draw_horizontal_line(rect->x, rect->x + rect->width - 1, rect->y, color);
        matrix_led_draw_horizontal_line(rect->x, rect->x + rect->width - 1, rect->y + rect->height - 1, color);
        matrix_led_draw_vertical_line(rect->x, rect->y, rect->y + rect->height - 1, color);
        matrix_led_draw_vertical_line(rect->x + rect->width - 1, rect->y, rect->y + rect->height - 1, color);
    }

    return ESP_OK;
}

esp_err_t matrix_led_draw_circle(uint8_t center_x, uint8_t center_y, uint8_t radius, matrix_led_color_t color, bool filled)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Midpoint circle algorithm
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        if (filled) {
            // 绘制水平线填充圆形
            matrix_led_draw_horizontal_line(center_x - x, center_x + x, center_y + y, color);
            matrix_led_draw_horizontal_line(center_x - x, center_x + x, center_y - y, color);
            matrix_led_draw_horizontal_line(center_x - y, center_x + y, center_y + x, color);
            matrix_led_draw_horizontal_line(center_x - y, center_x + y, center_y - x, color);
        } else {
            // 绘制圆形边框
            matrix_led_draw_pixel_safe(center_x + x, center_y + y, color);
            matrix_led_draw_pixel_safe(center_x + y, center_y + x, color);
            matrix_led_draw_pixel_safe(center_x - y, center_y + x, color);
            matrix_led_draw_pixel_safe(center_x - x, center_y + y, color);
            matrix_led_draw_pixel_safe(center_x - x, center_y - y, color);
            matrix_led_draw_pixel_safe(center_x - y, center_y - x, color);
            matrix_led_draw_pixel_safe(center_x + y, center_y - x, color);
            matrix_led_draw_pixel_safe(center_x + x, center_y - y, color);
        }

        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }

    return ESP_OK;
}

esp_err_t matrix_led_draw_text(uint8_t x, uint8_t y, const char* text, matrix_led_color_t color)
{
    if (!s_context.initialized || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 简单的8x8字体实现（仅支持基本ASCII字符）
    // 这里简化实现，实际项目中可以使用更完整的字体库
    ESP_LOGW(TAG, "Text rendering not fully implemented yet");
    
    return ESP_OK;
}

// ==================== 亮度控制API实现 ====================

esp_err_t matrix_led_set_brightness(uint8_t brightness)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > MATRIX_LED_MAX_BRIGHTNESS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t old_brightness = s_context.brightness;
    s_context.brightness = brightness;

    // 发送亮度变更事件
    matrix_led_event_data_t event_data = {
        .type = MATRIX_LED_EVENT_BRIGHTNESS_CHANGED,
        .data.brightness_change = {
            .old_brightness = old_brightness,
            .new_brightness = brightness
        }
    };
    matrix_led_send_event(MATRIX_LED_EVENT_BRIGHTNESS_CHANGED, &event_data);

    xSemaphoreGive(s_context.mutex);

    ESP_LOGI(TAG, "Brightness changed from %d%% to %d%%", old_brightness, brightness);

    return ESP_OK;
}

uint8_t matrix_led_get_brightness(void)
{
    return s_context.initialized ? s_context.brightness : 0;
}

// ==================== 动画控制API实现 ====================

esp_err_t matrix_led_set_mode(matrix_led_mode_t mode)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    matrix_led_mode_t old_mode = s_context.mode;
    s_context.mode = mode;

    // 根据模式切换停止或启动动画
    if (mode != MATRIX_LED_MODE_ANIMATION && s_context.animation.is_running) {
        matrix_led_stop_animation();
    }

    // 发送模式变更事件
    matrix_led_event_data_t event_data = {
        .type = MATRIX_LED_EVENT_MODE_CHANGED,
        .data.mode_change = {
            .old_mode = old_mode,
            .new_mode = mode
        }
    };
    matrix_led_send_event(MATRIX_LED_EVENT_MODE_CHANGED, &event_data);

    xSemaphoreGive(s_context.mutex);

    ESP_LOGI(TAG, "Display mode changed from %d to %d", old_mode, mode);

    return ESP_OK;
}

matrix_led_mode_t matrix_led_get_mode(void)
{
    return s_context.initialized ? s_context.mode : MATRIX_LED_MODE_OFF;
}

esp_err_t matrix_led_play_animation(matrix_led_animation_type_t animation_type, const matrix_led_animation_config_t* config)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (animation_type >= MATRIX_LED_ANIM_CUSTOM) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // 停止当前动画
    if (s_context.animation.is_running) {
        matrix_led_stop_animation();
    }

    // 设置动画参数
    s_context.animation.type = animation_type;
    s_context.animation.frame_counter = 0;
    s_context.animation.start_time = xTaskGetTickCount();
    s_context.animation.is_running = true;

    // 使用提供的配置或默认配置
    if (config != NULL) {
        s_context.animation.config = *config;
    } else {
        // 设置默认配置
        memset(&s_context.animation.config, 0, sizeof(matrix_led_animation_config_t));
        s_context.animation.config.type = animation_type;
        s_context.animation.config.duration_ms = 5000;  // 5秒
        s_context.animation.config.frame_delay_ms = 50;  // 20fps
        s_context.animation.config.loop = true;
        s_context.animation.config.speed = 50;
        s_context.animation.config.primary_color = MATRIX_LED_COLOR_BLUE;
        s_context.animation.config.secondary_color = MATRIX_LED_COLOR_RED;
        snprintf(s_context.animation.config.name, MATRIX_LED_MAX_NAME_LEN, "Anim_%d", animation_type);
    }

    // 切换到动画模式
    s_context.mode = MATRIX_LED_MODE_ANIMATION;

    // 启动动画定时器
    xTimerChangePeriod(s_context.animation_timer, 
                      pdMS_TO_TICKS(s_context.animation.config.frame_delay_ms), 0);
    xTimerStart(s_context.animation_timer, 0);

    // 发送动画开始事件
    matrix_led_event_data_t event_data = {
        .type = MATRIX_LED_EVENT_ANIMATION_STARTED,
        .data.animation = {
            .animation_name = {0}
        }
    };
    strncpy(event_data.data.animation.animation_name, s_context.animation.config.name, 
            MATRIX_LED_MAX_NAME_LEN - 1);
    matrix_led_send_event(MATRIX_LED_EVENT_ANIMATION_STARTED, &event_data);

    xSemaphoreGive(s_context.mutex);

    ESP_LOGI(TAG, "Animation started: %s (type: %d)", s_context.animation.config.name, animation_type);

    return ESP_OK;
}

esp_err_t matrix_led_stop_animation(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_context.animation.is_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_context.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // 停止动画定时器
    xTimerStop(s_context.animation_timer, 0);

    char animation_name[MATRIX_LED_MAX_NAME_LEN];
    strncpy(animation_name, s_context.animation.config.name, sizeof(animation_name));
    animation_name[MATRIX_LED_MAX_NAME_LEN - 1] = '\0';

    // 清空动画状态
    s_context.animation.is_running = false;
    s_context.animation.frame_counter = 0;
    memset(&s_context.animation.config, 0, sizeof(matrix_led_animation_config_t));

    // 发送动画停止事件
    matrix_led_event_data_t event_data = {
        .type = MATRIX_LED_EVENT_ANIMATION_STOPPED,
        .data.animation = {
            .animation_name = {0}
        }
    };
    strncpy(event_data.data.animation.animation_name, animation_name, MATRIX_LED_MAX_NAME_LEN - 1);
    matrix_led_send_event(MATRIX_LED_EVENT_ANIMATION_STOPPED, &event_data);

    xSemaphoreGive(s_context.mutex);

    ESP_LOGI(TAG, "Animation stopped: %s", animation_name);

    return ESP_OK;
}

esp_err_t matrix_led_load_animation_from_file(const char* filename, const char* animation_name)
{
    if (!s_context.initialized || filename == NULL || animation_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // TODO: 实现从文件加载动画的功能
    ESP_LOGW(TAG, "Load animation from file not implemented yet: %s", filename);
    
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t matrix_led_play_custom_animation(const char* animation_name)
{
    if (!s_context.initialized || animation_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // TODO: 实现播放自定义动画的功能
    ESP_LOGW(TAG, "Play custom animation not implemented yet: %s", animation_name);
    
    return ESP_ERR_NOT_SUPPORTED;
}

// ==================== 特效API实现 ====================

esp_err_t matrix_led_show_test_pattern(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Displaying test pattern");

    // 清空矩阵
    matrix_led_clear();

    // 绘制边框（红色）
    matrix_led_rect_t border = {0, 0, MATRIX_LED_WIDTH, MATRIX_LED_HEIGHT};
    matrix_led_draw_rect(&border, MATRIX_LED_COLOR_RED, false);

    // 绘制对角线（绿色）
    matrix_led_draw_line(0, 0, MATRIX_LED_WIDTH-1, MATRIX_LED_HEIGHT-1, MATRIX_LED_COLOR_GREEN);
    matrix_led_draw_line(0, MATRIX_LED_HEIGHT-1, MATRIX_LED_WIDTH-1, 0, MATRIX_LED_COLOR_GREEN);

    // 绘制中心十字（蓝色）
    uint8_t center_x = MATRIX_LED_WIDTH / 2;
    uint8_t center_y = MATRIX_LED_HEIGHT / 2;
    matrix_led_draw_line(center_x, 0, center_x, MATRIX_LED_HEIGHT-1, MATRIX_LED_COLOR_BLUE);
    matrix_led_draw_line(0, center_y, MATRIX_LED_WIDTH-1, center_y, MATRIX_LED_COLOR_BLUE);

    // 绘制中心圆形（黄色）
    matrix_led_draw_circle(center_x, center_y, 8, MATRIX_LED_COLOR_YELLOW, false);

    // 刷新显示
    matrix_led_refresh();

    return ESP_OK;
}

esp_err_t matrix_led_rainbow_gradient(uint8_t speed)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (speed > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    // 设置彩虹动画配置
    matrix_led_animation_config_t config = {
        .type = MATRIX_LED_ANIM_RAINBOW,
        .duration_ms = 0,  // 无限循环
        .frame_delay_ms = 100 - speed,  // 速度越大延迟越小
        .loop = true,
        .speed = speed,
        .primary_color = MATRIX_LED_COLOR_WHITE,
        .secondary_color = MATRIX_LED_COLOR_BLACK
    };
    snprintf(config.name, MATRIX_LED_MAX_NAME_LEN, "Rainbow_%d", speed);

    return matrix_led_play_animation(MATRIX_LED_ANIM_RAINBOW, &config);
}

esp_err_t matrix_led_breathe_effect(matrix_led_color_t color, uint8_t speed)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (speed > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    // 设置呼吸动画配置
    matrix_led_animation_config_t config = {
        .type = MATRIX_LED_ANIM_BREATHE,
        .duration_ms = 0,  // 无限循环
        .frame_delay_ms = 100 - speed,
        .loop = true,
        .speed = speed,
        .primary_color = color,
        .secondary_color = MATRIX_LED_COLOR_BLACK
    };
    snprintf(config.name, MATRIX_LED_MAX_NAME_LEN, "Breathe_%d", speed);

    return matrix_led_play_animation(MATRIX_LED_ANIM_BREATHE, &config);
}

// ==================== 颜色工具API实现 ====================

esp_err_t matrix_led_rgb_to_hsv(matrix_led_color_t rgb, matrix_led_hsv_t* hsv)
{
    if (hsv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;

    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float delta = max - min;

    // 计算亮度值和饱和度
    hsv->v = (uint8_t)(max * 100);
    hsv->s = (max == 0) ? 0 : (uint8_t)((delta / max) * 100);

    // 计算色相
    if (delta == 0) {
        hsv->h = 0;
    } else if (max == r) {
        float h_val = 60 * fmodf((g - b) / delta, 6);
        hsv->h = (h_val < 0) ? (uint16_t)(h_val + 360) : (uint16_t)h_val;
    } else if (max == g) {
        hsv->h = (uint16_t)(60 * ((b - r) / delta + 2));
    } else {
        hsv->h = (uint16_t)(60 * ((r - g) / delta + 4));
    }

    return ESP_OK;
}

esp_err_t matrix_led_hsv_to_rgb(matrix_led_hsv_t hsv, matrix_led_color_t* rgb)
{
    if (rgb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float h = hsv.h;
    float s = hsv.s / 100.0f;
    float v = hsv.v / 100.0f;

    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;

    float r, g, b;

    if (h >= 0 && h < 60) {
        r = c; g = x; b = 0;
    } else if (h >= 60 && h < 120) {
        r = x; g = c; b = 0;
    } else if (h >= 120 && h < 180) {
        r = 0; g = c; b = x;
    } else if (h >= 180 && h < 240) {
        r = 0; g = x; b = c;
    } else if (h >= 240 && h < 300) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }

    rgb->r = (uint8_t)((r + m) * 255);
    rgb->g = (uint8_t)((g + m) * 255);
    rgb->b = (uint8_t)((b + m) * 255);

    return ESP_OK;
}

esp_err_t matrix_led_color_interpolate(matrix_led_color_t color1, matrix_led_color_t color2, float ratio, matrix_led_color_t* result)
{
    if (result == NULL || ratio < 0.0f || ratio > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    result->r = (uint8_t)(color1.r + (color2.r - color1.r) * ratio);
    result->g = (uint8_t)(color1.g + (color2.g - color1.g) * ratio);
    result->b = (uint8_t)(color1.b + (color2.b - color1.b) * ratio);

    return ESP_OK;
}

esp_err_t matrix_led_apply_brightness(matrix_led_color_t color, uint8_t brightness, matrix_led_color_t* result)
{
    if (result == NULL || brightness > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    float factor = brightness / 100.0f;
    result->r = (uint8_t)(color.r * factor);
    result->g = (uint8_t)(color.g * factor);
    result->b = (uint8_t)(color.b * factor);

    return ESP_OK;
}

// ==================== 配置管理API实现 ====================

esp_err_t matrix_led_save_config(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    
    uint8_t brightness = s_context.brightness;
    ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_BRIGHTNESS, CONFIG_TYPE_UINT8, &brightness, sizeof(brightness));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness config: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t mode = (uint8_t)s_context.mode;
    ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_MODE, CONFIG_TYPE_UINT8, &mode, sizeof(mode));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save mode config: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t enabled = s_context.enabled ? 1 : 0;
    ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_ENABLE, CONFIG_TYPE_UINT8, &enabled, sizeof(enabled));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save enable config: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保存动画配置
    if (s_context.animation.is_running) {
        // 保存动画类型
        uint8_t anim_type = (uint8_t)s_context.animation.type;
        ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, "anim_type", CONFIG_TYPE_UINT8, &anim_type, sizeof(anim_type));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save animation type: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 保存动画速度
        uint8_t anim_speed = s_context.animation.config.speed;
        ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, "anim_speed", CONFIG_TYPE_UINT8, &anim_speed, sizeof(anim_speed));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save animation speed: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 保存动画名称
        size_t str_len = strlen(s_context.animation.config.name) + 1;
        ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_ANIMATION,
                                CONFIG_TYPE_STRING, s_context.animation.config.name, str_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save animation config: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Animation config saved: %s (type: %d, speed: %d)", 
                 s_context.animation.config.name, anim_type, anim_speed);
    } else {
        // 清除动画配置 (静默处理 - 不显示"未找到"错误)
        ESP_LOGI(TAG, "Saving static image configuration...");
        
        // 静默删除动画相关配置，忽略所有错误
        config_manager_delete(MATRIX_LED_CONFIG_NAMESPACE, "anim_type");
        config_manager_delete(MATRIX_LED_CONFIG_NAMESPACE, "anim_speed");
        config_manager_delete(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_ANIMATION);
        
        // 当没有动画运行时，保存当前静态内容
        if (s_context.pixel_buffer != NULL) {
            // 保存像素缓冲区数据
            size_t data_size = MATRIX_LED_COUNT * sizeof(matrix_led_color_t);
            ret = config_manager_set(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_STATIC_DATA,
                                    CONFIG_TYPE_BLOB, s_context.pixel_buffer, data_size);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Static LED data saved (%zu bytes)", data_size);
            } else {
                ESP_LOGW(TAG, "Failed to save static LED data: %s", esp_err_to_name(ret));
            }
        }
    }

    ret = config_manager_commit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Matrix LED configuration saved successfully");
    return ESP_OK;
}

esp_err_t matrix_led_load_config(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    uint8_t value_u8;
    size_t required_size;
    char* animation_name = NULL;

    // 加载亮度配置
    size_t value_size = sizeof(value_u8);
    ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_BRIGHTNESS, CONFIG_TYPE_UINT8, &value_u8, &value_size);
    if (ret == ESP_OK) {
        s_context.brightness = value_u8;
    }

    // 加载模式配置
    size_t mode_size = sizeof(value_u8);
    ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_MODE, CONFIG_TYPE_UINT8, &value_u8, &mode_size);
    if (ret == ESP_OK && value_u8 < MATRIX_LED_MODE_OFF) {
        s_context.mode = (matrix_led_mode_t)value_u8;
    }

    // 加载启用配置
    size_t enable_size = sizeof(value_u8);
    ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_ENABLE, CONFIG_TYPE_UINT8, &value_u8, &enable_size);
    if (ret == ESP_OK) {
        s_context.enabled = (value_u8 != 0);
    }

    // 加载动画配置
    bool should_start_animation = false;
    matrix_led_animation_type_t saved_anim_type = MATRIX_LED_ANIM_RAINBOW;
    uint8_t saved_anim_speed = 50;
    
    ESP_LOGI(TAG, "Attempting to load animation configuration...");
    required_size = 0;  // 初始化大小变量
    ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_ANIMATION, CONFIG_TYPE_STRING, NULL, &required_size);
    if (ret == ESP_OK && required_size > 0) {
        animation_name = malloc(required_size);
        if (animation_name != NULL) {
            ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_ANIMATION, 
                                     CONFIG_TYPE_STRING, animation_name, &required_size);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Loaded animation config: %s", animation_name);
                
                // 加载动画类型
                uint8_t anim_type_u8;
                size_t type_size = sizeof(anim_type_u8);
                ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, "anim_type", CONFIG_TYPE_UINT8, &anim_type_u8, &type_size);
                if (ret == ESP_OK) {
                    saved_anim_type = (matrix_led_animation_type_t)anim_type_u8;
                }
                
                // 加载动画速度
                size_t speed_size = sizeof(saved_anim_speed);
                config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, "anim_speed", CONFIG_TYPE_UINT8, &saved_anim_speed, &speed_size);
                
                if (strcmp(animation_name, "test") != 0) {
                    should_start_animation = true;
                    ESP_LOGI(TAG, "Will restore animation: %s (type: %d, speed: %d)", 
                             animation_name, saved_anim_type, saved_anim_speed);
                }
            }
            free(animation_name);
        }
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No animation configuration found - starting fresh");
    } else {
        ESP_LOGW(TAG, "Error querying animation config: %s", esp_err_to_name(ret));
    }

    // 应用加载的配置
    if (!s_context.enabled) {
        matrix_led_clear();
        matrix_led_refresh();
        ESP_LOGI(TAG, "Matrix LED disabled by configuration");
    } else if (should_start_animation) {
        // 恢复动画播放
        matrix_led_animation_config_t config = {0};
        config.type = saved_anim_type;
        config.speed = saved_anim_speed;
        config.loop = true;
        config.frame_delay_ms = 100 - saved_anim_speed;
        config.primary_color = MATRIX_LED_COLOR_BLUE;
        config.secondary_color = MATRIX_LED_COLOR_RED;
        
        const char* anim_names[] = {"rainbow", "wave", "breathe", "rotate", "fade"};
        if (saved_anim_type < sizeof(anim_names)/sizeof(anim_names[0])) {
            snprintf(config.name, MATRIX_LED_MAX_NAME_LEN, "%s_%d", anim_names[saved_anim_type], saved_anim_speed);
        }
        
        esp_err_t anim_ret = matrix_led_play_animation(saved_anim_type, &config);
        if (anim_ret == ESP_OK) {
            ESP_LOGI(TAG, "Animation restored successfully: %s", config.name);
        } else {
            ESP_LOGE(TAG, "Failed to restore animation: %s", esp_err_to_name(anim_ret));
            matrix_led_clear();
            matrix_led_refresh();
        }
    } else {
        // 没有动画配置，尝试恢复静态内容
        size_t static_data_size = 0;
        ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_STATIC_DATA, 
                                CONFIG_TYPE_BLOB, NULL, &static_data_size);
        if (ret == ESP_OK && static_data_size == MATRIX_LED_COUNT * sizeof(matrix_led_color_t)) {
            // 恢复静态像素数据
            ret = config_manager_get(MATRIX_LED_CONFIG_NAMESPACE, MATRIX_LED_CONFIG_KEY_STATIC_DATA,
                                    CONFIG_TYPE_BLOB, s_context.pixel_buffer, &static_data_size);
            if (ret == ESP_OK) {
                matrix_led_refresh();
                ESP_LOGI(TAG, "Static LED content restored (%zu bytes)", static_data_size);
            } else {
                matrix_led_clear();
                matrix_led_refresh();
                ESP_LOGI(TAG, "Failed to restore static content, display cleared");
            }
        } else {
            // 清除测试图案，显示黑屏
            matrix_led_clear();
            matrix_led_refresh();
            ESP_LOGI(TAG, "Matrix LED cleared (no animation or static content to restore)");
        }
    }

    ESP_LOGI(TAG, "Matrix LED configuration loaded (brightness: %d%%, mode: %d, enabled: %s)", 
             s_context.brightness, s_context.mode, s_context.enabled ? "yes" : "no");

    return ESP_OK;
}

esp_err_t matrix_led_reset_config(void)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 恢复默认值
    s_context.brightness = MATRIX_LED_DEFAULT_BRIGHTNESS;
    s_context.mode = MATRIX_LED_MODE_STATIC;
    s_context.enabled = true;

    // 停止动画
    if (s_context.animation.is_running) {
        matrix_led_stop_animation();
    }

    // 清空显示
    matrix_led_clear();
    matrix_led_refresh();

    ESP_LOGI(TAG, "Matrix LED configuration reset to defaults");

    return ESP_OK;
}

// ==================== 静态函数实现 ====================

static esp_err_t matrix_led_init_hardware(void)
{
    ESP_LOGI(TAG, "Initializing LED strip hardware...");

    // LED条带配置
    led_strip_config_t strip_config = {
        .strip_gpio_num = MATRIX_LED_GPIO,
        .max_leds = MATRIX_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        }
    };

    // RMT后端配置
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = MATRIX_LED_RMT_RESOLUTION,
        .flags = {
            .with_dma = true,  // 使用DMA提高性能
        }
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_context.led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    // 清空LED条带
    ret = led_strip_clear(s_context.led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip: %s", esp_err_to_name(ret));
        led_strip_del(s_context.led_strip);
        s_context.led_strip = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "LED strip hardware initialized successfully");
    return ESP_OK;
}

static esp_err_t matrix_led_deinit_hardware(void)
{
    if (s_context.led_strip) {
        led_strip_clear(s_context.led_strip);
        led_strip_refresh(s_context.led_strip);
        led_strip_del(s_context.led_strip);
        s_context.led_strip = NULL;
    }
    
    ESP_LOGI(TAG, "LED strip hardware deinitialized");
    return ESP_OK;
}

static void matrix_led_animation_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Matrix LED animation task started");

    // 等待初始化完成
    while (!s_context.initialized) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (s_context.initialized) {
        // 等待动画信号量或超时
        if (xSemaphoreTake(s_context.animation_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // 执行动画更新
            if (s_context.animation.is_running && s_context.enabled) {
                switch (s_context.animation.type) {
                    case MATRIX_LED_ANIM_RAINBOW:
                        matrix_led_animate_rainbow();
                        break;
                    case MATRIX_LED_ANIM_WAVE:
                        matrix_led_animate_wave();
                        break;
                    case MATRIX_LED_ANIM_BREATHE:
                        matrix_led_animate_breathe();
                        break;
                    case MATRIX_LED_ANIM_ROTATE:
                        matrix_led_animate_rotate();
                        break;
                    case MATRIX_LED_ANIM_FADE:
                        matrix_led_animate_fade();
                        break;
                    default:
                        break;
                }
                
                // 刷新显示
                matrix_led_refresh();
                
                s_context.animation.frame_counter++;
            }
        }
        
        // 休眠一段时间
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TAG, "Matrix LED animation task ended");
    vTaskDelete(NULL);
}

static void matrix_led_animation_timer_callback(TimerHandle_t xTimer)
{
    // 发送动画信号量触发动画更新
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_context.animation_semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static esp_err_t __attribute__((unused)) matrix_led_load_default_config(void)
{
    s_context.brightness = MATRIX_LED_DEFAULT_BRIGHTNESS;
    s_context.mode = MATRIX_LED_MODE_STATIC;
    s_context.enabled = true;
    
    return ESP_OK;
}

static esp_err_t matrix_led_validate_coordinates(uint8_t x, uint8_t y)
{
    if (x >= MATRIX_LED_WIDTH || y >= MATRIX_LED_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static uint32_t matrix_led_xy_to_index(uint8_t x, uint8_t y)
{
    // 转换坐标为LED索引（行优先顺序）
    return y * MATRIX_LED_WIDTH + x;
}

static void __attribute__((unused)) matrix_led_index_to_xy(uint32_t index, uint8_t* x, uint8_t* y)
{
    if (x && y && index < MATRIX_LED_COUNT) {
        *y = index / MATRIX_LED_WIDTH;
        *x = index % MATRIX_LED_WIDTH;
    }
}

static esp_err_t matrix_led_send_event(matrix_led_event_type_t type, const matrix_led_event_data_t* data)
{
    // 使用事件管理器发送事件
    ESP_EVENT_DECLARE_BASE(MATRIX_LED_EVENTS);
    return event_manager_post_event(MATRIX_LED_EVENTS, type, (void*)data, sizeof(matrix_led_event_data_t), portMAX_DELAY);
}

// ==================== 动画函数实现 ====================

static void matrix_led_animate_rainbow(void)
{
    uint32_t time_offset = (xTaskGetTickCount() - s_context.animation.start_time) * s_context.animation.config.speed / 10;
    
    for (uint8_t y = 0; y < MATRIX_LED_HEIGHT; y++) {
        for (uint8_t x = 0; x < MATRIX_LED_WIDTH; x++) {
            uint16_t hue = ((x + y) * 360 / (MATRIX_LED_WIDTH + MATRIX_LED_HEIGHT) + time_offset) % 360;
            matrix_led_hsv_t hsv = {hue, 100, 100};
            matrix_led_color_t rgb;
            
            if (matrix_led_hsv_to_rgb(hsv, &rgb) == ESP_OK) {
                uint32_t index = matrix_led_xy_to_index(x, y);
                s_context.pixel_buffer[index] = rgb;
            }
        }
    }
}

static void matrix_led_animate_wave(void)
{
    uint32_t time_offset = (xTaskGetTickCount() - s_context.animation.start_time) * s_context.animation.config.speed / 20;
    
    matrix_led_color_t primary = s_context.animation.config.primary_color;
    matrix_led_color_t secondary = s_context.animation.config.secondary_color;
    
    for (uint8_t y = 0; y < MATRIX_LED_HEIGHT; y++) {
        for (uint8_t x = 0; x < MATRIX_LED_WIDTH; x++) {
            float wave = sinf((x + time_offset) * 0.2f) * 0.5f + 0.5f;
            matrix_led_color_t result;
            
            if (matrix_led_color_interpolate(secondary, primary, wave, &result) == ESP_OK) {
                uint32_t index = matrix_led_xy_to_index(x, y);
                s_context.pixel_buffer[index] = result;
            }
        }
    }
}

static void matrix_led_animate_breathe(void)
{
    uint32_t time_offset = (xTaskGetTickCount() - s_context.animation.start_time) * s_context.animation.config.speed / 50;
    float breathe = (sinf(time_offset * 0.1f) + 1.0f) / 2.0f;
    
    matrix_led_color_t base_color = s_context.animation.config.primary_color;
    matrix_led_color_t result;
    
    if (matrix_led_apply_brightness(base_color, (uint8_t)(breathe * 100), &result) == ESP_OK) {
        matrix_led_fill(result);
    }
}

static void matrix_led_animate_rotate(void)
{
    // 简单的旋转动画实现
    uint32_t time_offset = (xTaskGetTickCount() - s_context.animation.start_time) * s_context.animation.config.speed / 30;
    
    matrix_led_clear();
    
    uint8_t center_x = MATRIX_LED_WIDTH / 2;
    uint8_t center_y = MATRIX_LED_HEIGHT / 2;
    uint8_t radius = 12;
    
    for (int i = 0; i < 4; i++) {
        float angle = (time_offset + i * 90) * M_PI / 180.0f;
        uint8_t x = center_x + (uint8_t)(cosf(angle) * radius);
        uint8_t y = center_y + (uint8_t)(sinf(angle) * radius);
        
        if (x < MATRIX_LED_WIDTH && y < MATRIX_LED_HEIGHT) {
            matrix_led_draw_pixel_safe(x, y, s_context.animation.config.primary_color);
        }
    }
}

static void matrix_led_animate_fade(void)
{
    uint32_t time_offset = (xTaskGetTickCount() - s_context.animation.start_time) * s_context.animation.config.speed / 40;
    float fade = (sinf(time_offset * 0.05f) + 1.0f) / 2.0f;
    
    matrix_led_color_t color1 = s_context.animation.config.primary_color;
    matrix_led_color_t color2 = s_context.animation.config.secondary_color;
    matrix_led_color_t result;
    
    if (matrix_led_color_interpolate(color1, color2, fade, &result) == ESP_OK) {
        matrix_led_fill(result);
    }
}

// ==================== 图形绘制辅助函数 ====================

static void matrix_led_draw_pixel_safe(uint8_t x, uint8_t y, matrix_led_color_t color)
{
    if (x < MATRIX_LED_WIDTH && y < MATRIX_LED_HEIGHT) {
        matrix_led_set_pixel(x, y, color);
    }
}

static void matrix_led_draw_horizontal_line(uint8_t x0, uint8_t x1, uint8_t y, matrix_led_color_t color)
{
    if (y >= MATRIX_LED_HEIGHT) return;
    
    uint8_t start_x = (x0 < x1) ? x0 : x1;
    uint8_t end_x = (x0 < x1) ? x1 : x0;
    
    if (end_x >= MATRIX_LED_WIDTH) end_x = MATRIX_LED_WIDTH - 1;
    
    for (uint8_t x = start_x; x <= end_x; x++) {
        matrix_led_draw_pixel_safe(x, y, color);
    }
}

static void matrix_led_draw_vertical_line(uint8_t x, uint8_t y0, uint8_t y1, matrix_led_color_t color)
{
    if (x >= MATRIX_LED_WIDTH) return;
    
    uint8_t start_y = (y0 < y1) ? y0 : y1;
    uint8_t end_y = (y0 < y1) ? y1 : y0;
    
    if (end_y >= MATRIX_LED_HEIGHT) end_y = MATRIX_LED_HEIGHT - 1;
    
    for (uint8_t y = start_y; y <= end_y; y++) {
        matrix_led_draw_pixel_safe(x, y, color);
    }
}

// ==================== 控制台命令实现 ====================

int matrix_led_cmd_handler(int argc, char **argv)
{
    // 检查是否是led matrix命令的调用
    if (argc < 1 || (argc >= 1 && strcmp(argv[0], "matrix") != 0)) {
        // 如果不是matrix子命令，显示帮助
        printf("Matrix LED Commands:\n");
        printf("Basic Control:\n");
        printf("  led matrix status                    - Show matrix status\n");
        printf("  led matrix enable <on|off>           - Enable/disable matrix\n");
        printf("  led matrix brightness <0-100>        - Set brightness\n");
        printf("  led matrix clear                     - Clear all pixels\n");
        printf("  led matrix fill <r> <g> <b>          - Fill with color\n");
        printf("  led matrix pixel <x> <y> <r> <g> <b> - Set pixel color\n");
        printf("  led matrix test                      - Show test pattern\n");
        printf("Animation Control:\n");
        printf("  led matrix mode <static|animation|off> - Set display mode\n");
        printf("  led matrix anim <type> [speed]       - Play animation\n");
        printf("    Types: rainbow, wave, breathe, rotate, fade\n");
        printf("  led matrix stop                      - Stop animation\n");
        printf("Drawing Commands:\n");
        printf("  led matrix draw line <x0> <y0> <x1> <y1> <r> <g> <b> - Draw line\n");
        printf("  led matrix draw rect <x> <y> <w> <h> <r> <g> <b> [fill] - Draw rectangle\n");
        printf("  led matrix draw circle <x> <y> <radius> <r> <g> <b> [fill] - Draw circle\n");
        printf("Configuration:\n");
        printf("  led matrix config <save|load|reset|export|import> - Config management\n");
        printf("Storage Features:\n");
        printf("  led matrix image <export|import> <filepath> - Image save/load\n");
        printf("  led matrix storage status             - Check storage availability\n");
        printf("  led matrix storage test               - Test file operations\n");
        printf("  led matrix storage testwrite         - Test config file write\n");
        return 0;
    }

    if (argc < 2) {
        // Show help when no arguments provided
        printf("Matrix LED Controller - 32x32 WS2812 LED Matrix Commands\n");
        printf("═══════════════════════════════════════════════════════════\n");
        printf("Basic Control:\n");
        printf("  led matrix status                 - Show current status\n");
        printf("  led matrix enable <on|off>       - Enable/disable matrix\n");
        printf("  led matrix brightness <0-100>    - Set brightness percentage\n");
        printf("  led matrix clear                 - Clear all LEDs\n");
        printf("  led matrix test                  - Show test pattern\n");
        printf("\nDrawing Commands:\n");
        printf("  led matrix fill <r> <g> <b>      - Fill with color (0-255)\n");
        printf("  led matrix pixel <x> <y> <r> <g> <b> - Set single pixel\n");
        printf("  led matrix draw line <x0> <y0> <x1> <y1> <r> <g> <b>\n");
        printf("  led matrix draw rect <x> <y> <w> <h> <r> <g> <b> [fill]\n");
        printf("  led matrix draw circle <x> <y> <radius> <r> <g> <b> [fill]\n");
        printf("\nAnimation Commands:\n");
        printf("  led matrix anim <type> [speed]   - Start animation\n");
        printf("    Types: rainbow, wave, breathe, rotate, fade\n");
        printf("    Speed: 1-100 (default: 50)\n");
        printf("  led matrix stop                  - Stop current animation\n");
        printf("\nMode Control:\n");
        printf("  led matrix mode <static|animation|off> - Set display mode\n");
        printf("\nConfiguration:\n");
        printf("  led matrix config save           - Save current settings\n");
        printf("  led matrix config load           - Reload saved settings\n");
        printf("\nHelp:\n");
        printf("  led matrix help                  - Show this help message\n");
        printf("\nExamples:\n");
        printf("  led matrix fill 255 0 0          - Fill with red\n");
        printf("  led matrix pixel 16 16 0 255 0   - Green center pixel\n");
        printf("  led matrix anim rainbow 75       - Fast rainbow animation\n");
        printf("  led matrix brightness 30         - Set to 30%% brightness\n");
        return 0;
    }

    esp_err_t ret = ESP_OK;

    if (strcmp(argv[1], "help") == 0) {
        // Show detailed help
        printf("Matrix LED Controller - 32x32 WS2812 LED Matrix Commands\n");
        printf("═══════════════════════════════════════════════════════════\n");
        printf("Basic Control:\n");
        printf("  led matrix status                 - Show current status\n");
        printf("  led matrix enable <on|off>       - Enable/disable matrix\n");
        printf("  led matrix brightness <0-100>    - Set brightness percentage\n");
        printf("  led matrix clear                 - Clear all LEDs\n");
        printf("  led matrix test                  - Show test pattern\n");
        printf("\nDrawing Commands:\n");
        printf("  led matrix fill <r> <g> <b>      - Fill with color (0-255)\n");
        printf("  led matrix pixel <x> <y> <r> <g> <b> - Set single pixel\n");
        printf("  led matrix draw line <x0> <y0> <x1> <y1> <r> <g> <b>\n");
        printf("  led matrix draw rect <x> <y> <w> <h> <r> <g> <b> [fill]\n");
        printf("  led matrix draw circle <x> <y> <radius> <r> <g> <b> [fill]\n");
        printf("\nAnimation Commands:\n");
        printf("  led matrix anim <type> [speed]   - Start animation\n");
        printf("    Types: rainbow, wave, breathe, rotate, fade\n");
        printf("    Speed: 1-100 (default: 50)\n");
        printf("  led matrix stop                  - Stop current animation\n");
        printf("\nMode Control:\n");
        printf("  led matrix mode <static|animation|off> - Set display mode\n");
        printf("\nConfiguration:\n");
        printf("  led matrix config save           - Save to NVS memory\n");
        printf("  led matrix config load           - Load from NVS memory\n");
        printf("  led matrix config export <file>  - Export to SD card\n");
        printf("  led matrix config import <file>  - Import from SD card\n");
        printf("\nStorage Features:\n");
        printf("  led matrix image export <file>     - Save current display (points format)\n");
        printf("  led matrix image import <file> [name] - Load image from file (optionally by name)\n");
        printf("  led matrix image list <file>       - List all animations in file\n");
        printf("  led matrix storage status        - Check SD card status\n");
        printf("  led matrix storage test          - Test file operations\n");
        printf("  led matrix storage testwrite     - Test config file write\n");
        printf("\nHelp:\n");
        printf("  led matrix help                  - Show this help message\n");
        printf("\nExamples:\n");
        printf("  led matrix fill 255 0 0          - Fill with red\n");
        printf("  led matrix pixel 16 16 0 255 0   - Green center pixel\n");
        printf("  led matrix anim rainbow 75       - Fast rainbow animation\n");
        printf("  led matrix brightness 30         - Set to 30%% brightness\n");
        printf("  led matrix config export /sdcard/config.json\n");
        printf("  led matrix image export /sdcard/matrix.json\n");
        printf("\nCoordinate System:\n");
        printf("  Origin (0,0) is at top-left corner\n");
        printf("  X-axis: 0-31 (left to right)\n");
        printf("  Y-axis: 0-31 (top to bottom)\n");
        printf("  Colors: RGB values 0-255\n");
        return 0;
    }
    else if (strcmp(argv[1], "status") == 0) {
        matrix_led_status_t status;
        ret = matrix_led_get_status(&status);
        if (ret == ESP_OK) {
            printf("Matrix LED Status:\n");
            printf("  Initialized: %s\n", status.initialized ? "Yes" : "No");
            printf("  Enabled: %s\n", status.enabled ? "Yes" : "No");
            printf("  Mode: %d\n", status.mode);
            printf("  Brightness: %d%%\n", status.brightness);
            printf("  Pixel Count: %lu\n", status.pixel_count);
            printf("  Frame Count: %lu\n", status.frame_count);
            if (strlen(status.current_animation) > 0) {
                printf("  Current Animation: %s\n", status.current_animation);
            }
        }
    }
    else if (strcmp(argv[1], "enable") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix enable <on|off>\n");
            return 1;
        }
        bool enable = (strcmp(argv[2], "on") == 0);
        ret = matrix_led_set_enable(enable);
        if (ret == ESP_OK) {
            printf("Matrix LED %s\n", enable ? "enabled" : "disabled");
        }
    }
    else if (strcmp(argv[1], "brightness") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix brightness <0-100>\n");
            return 1;
        }
        int brightness = atoi(argv[2]);
        if (brightness >= 0 && brightness <= 100) {
            ret = matrix_led_set_brightness(brightness);
            if (ret == ESP_OK) {
                printf("Matrix brightness set to %d%%\n", brightness);
                matrix_led_refresh();  // 立即刷新显示效果
            }
        } else {
            printf("Brightness must be 0-100\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "clear") == 0) {
        // Stop any running animation first (only if there is one)
        bool had_animation = s_context.animation.is_running;
        if (had_animation) {
            ret = matrix_led_stop_animation();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to stop animation before clear: %s", esp_err_to_name(ret));
            }
        }
        
        ret = matrix_led_clear();
        if (ret == ESP_OK) {
            matrix_led_refresh();
            if (had_animation) {
                printf("Matrix cleared and animations stopped\n");
            } else {
                printf("Matrix cleared\n");
            }
        }
    }
    else if (strcmp(argv[1], "fill") == 0) {
        if (argc < 5) {
            printf("Usage: led matrix fill <r> <g> <b>\n");
            return 1;
        }
        int r = atoi(argv[2]);
        int g = atoi(argv[3]);
        int b = atoi(argv[4]);
        if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
            matrix_led_color_t color = {r, g, b};
            ret = matrix_led_fill(color);
            if (ret == ESP_OK) {
                matrix_led_refresh();
                printf("Matrix filled with RGB(%d,%d,%d)\n", r, g, b);
            }
        } else {
            printf("RGB values must be 0-255\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "pixel") == 0) {
        if (argc < 7) {
            printf("Usage: led matrix pixel <x> <y> <r> <g> <b>\n");
            return 1;
        }
        int x = atoi(argv[2]);
        int y = atoi(argv[3]);
        int r = atoi(argv[4]);
        int g = atoi(argv[5]);
        int b = atoi(argv[6]);
        if (x >= 0 && x < MATRIX_LED_WIDTH && y >= 0 && y < MATRIX_LED_HEIGHT &&
            r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
            matrix_led_color_t color = {r, g, b};
            ret = matrix_led_set_pixel(x, y, color);
            if (ret == ESP_OK) {
                matrix_led_refresh();
                printf("Pixel (%d,%d) set to RGB(%d,%d,%d)\n", x, y, r, g, b);
            }
        } else {
            printf("Invalid coordinates or RGB values\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "test") == 0) {
        ret = matrix_led_show_test_pattern();
        if (ret == ESP_OK) {
            printf("Test pattern displayed\n");
        }
    }
    else if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix mode <static|animation|off>\n");
            return 1;
        }
        matrix_led_mode_t mode;
        if (strcmp(argv[2], "static") == 0) {
            mode = MATRIX_LED_MODE_STATIC;
        } else if (strcmp(argv[2], "animation") == 0) {
            mode = MATRIX_LED_MODE_ANIMATION;
        } else if (strcmp(argv[2], "off") == 0) {
            mode = MATRIX_LED_MODE_OFF;
        } else {
            printf("Invalid mode. Use: static, animation, or off\n");
            return 1;
        }
        ret = matrix_led_set_mode(mode);
        if (ret == ESP_OK) {
            printf("Display mode set to %s\n", argv[2]);
        }
    }
    else if (strcmp(argv[1], "anim") == 0 || strcmp(argv[1], "animation") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix anim <type> [speed]\n");
            printf("Types: rainbow, wave, breathe, rotate, fade\n");
            return 1;
        }
        
        matrix_led_animation_type_t anim_type;
        if (strcmp(argv[2], "rainbow") == 0) {
            anim_type = MATRIX_LED_ANIM_RAINBOW;
        } else if (strcmp(argv[2], "wave") == 0) {
            anim_type = MATRIX_LED_ANIM_WAVE;
        } else if (strcmp(argv[2], "breathe") == 0) {
            anim_type = MATRIX_LED_ANIM_BREATHE;
        } else if (strcmp(argv[2], "rotate") == 0) {
            anim_type = MATRIX_LED_ANIM_ROTATE;
        } else if (strcmp(argv[2], "fade") == 0) {
            anim_type = MATRIX_LED_ANIM_FADE;
        } else {
            printf("Invalid animation type\n");
            return 1;
        }
        
        int speed = 50;  // 默认速度
        if (argc >= 4) {
            speed = atoi(argv[3]);
            if (speed < 0 || speed > 100) {
                printf("Speed must be 0-100\n");
                return 1;
            }
        }
        
        matrix_led_animation_config_t config = {0};
        config.type = anim_type;
        config.speed = speed;
        config.loop = true;
        config.frame_delay_ms = 100 - speed;
        config.primary_color = MATRIX_LED_COLOR_BLUE;
        config.secondary_color = MATRIX_LED_COLOR_RED;
        snprintf(config.name, MATRIX_LED_MAX_NAME_LEN, "%s_%d", argv[2], speed);
        
        ret = matrix_led_play_animation(anim_type, &config);
        if (ret == ESP_OK) {
            printf("Animation '%s' started with speed %d\n", argv[2], speed);
        }
    }
    else if (strcmp(argv[1], "stop") == 0) {
        ret = matrix_led_stop_animation();
        if (ret == ESP_OK) {
            printf("Animation stopped\n");
        }
    }
    else if (strcmp(argv[1], "config") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix config <save|load|reset|export|import>\n");
            printf("  save   - Save to NVS memory\n");
            printf("  load   - Load from NVS memory\n");
            printf("  reset  - Reset to defaults\n");
            printf("  export <filepath> - Export to SD card file\n");
            printf("  import <filepath> - Import from SD card file\n");
            return 1;
        }
        if (strcmp(argv[2], "save") == 0) {
            printf("Saving matrix LED configuration...\n");
            ret = matrix_led_save_config();
            if (ret == ESP_OK) {
                printf("✓ Configuration saved successfully to NVS memory\n");
                printf("  Current display will be restored after reboot\n");
            } else {
                printf("✗ Failed to save configuration: %s\n", esp_err_to_name(ret));
            }
        } else if (strcmp(argv[2], "load") == 0) {
            ret = matrix_led_load_config();
            if (ret == ESP_OK) {
                printf("Matrix LED configuration loaded from NVS\n");
            }
        } else if (strcmp(argv[2], "reset") == 0) {
            ret = matrix_led_reset_config();
            if (ret == ESP_OK) {
                printf("Matrix LED configuration reset to defaults\n");
            }
        } else if (strcmp(argv[2], "export") == 0) {
            if (argc < 4) {
                printf("Usage: led matrix config export <filepath>\n");
                printf("Example: led matrix config export /sdcard/config.json\n");
                printf("         led matrix config export /sdcard/led/config.json\n");
                printf("         led matrix config export /sdcard/my_config.json\n");
                return 1;
            }
            
            const char* filepath = argv[3];
            
            ret = matrix_led_export_config(filepath);
            
            if (ret == ESP_OK) {
                printf("Configuration exported to: %s\n", filepath);
            } else if (ret == ESP_ERR_INVALID_STATE && !matrix_led_storage_available()) {
                printf("SD card not available for export\n");
            } else {
                printf("Export failed: %s\n", esp_err_to_name(ret));
            }
            
            if (ret == ESP_OK && strchr(filepath + 8, '/') == NULL) {
                printf("Configuration exported to: %s\n", filepath);
            } else if (ret == ESP_ERR_INVALID_STATE && !matrix_led_storage_available()) {
                printf("SD card not available for export\n");
            } else if (ret != ESP_OK) {
                printf("Export failed. Try using: /sdcard/led/config.json\n");
            }
        } else if (strcmp(argv[2], "import") == 0) {
            if (argc < 4) {
                printf("Usage: led matrix config import <filepath>\n");
                printf("Example: led matrix config import /sdcard/config.json\n");
                printf("         led matrix config import /sdcard/led/config.json\n");
                return 1;
            }
            ret = matrix_led_import_config(argv[3]);
            if (ret == ESP_OK) {
                printf("Configuration imported from: %s\n", argv[3]);
            } else if (ret == ESP_ERR_NOT_FOUND) {
                printf("Configuration file not found: %s\n", argv[3]);
            }
        } else {
            printf("Invalid config command\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "image") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix image <export|import> <filepath>\n");
            printf("  export <filepath> - Save current display to file\n");
            printf("  import <filepath> - Load image from file\n");
            return 1;
        }
        
        if (strcmp(argv[2], "export") == 0) {
            if (argc < 4) {
                printf("Usage: led matrix image export <filepath>\n");
                printf("Example: led matrix image export /sdcard/matrix.json\n");
                return 1;
            }
            ret = matrix_led_export_image(argv[3]);
            if (ret == ESP_OK) {
                printf("Image exported to: %s\n", argv[3]);
            } else if (ret == ESP_ERR_INVALID_STATE && !matrix_led_storage_available()) {
                printf("SD card not available for export\n");
            }
        } else if (strcmp(argv[2], "import") == 0) {
            if (argc < 4) {
                printf("Usage: led matrix image import <filepath> [name]\n");
                printf("Example: led matrix image import /sdcard/logo.json\n");
                printf("Example: led matrix image import /sdcard/logo.json Logo\n");
                return 1;
            }
            
            const char* animation_name = (argc >= 5) ? argv[4] : NULL;
            ret = matrix_led_import_image_by_name(argv[3], animation_name);
            
            if (ret == ESP_OK) {
                if (animation_name) {
                    printf("Image imported from: %s (%s)\n", argv[3], animation_name);
                } else {
                    printf("Image imported from: %s\n", argv[3]);
                }
            } else if (ret == ESP_ERR_NOT_FOUND) {
                if (animation_name) {
                    printf("Animation '%s' not found in file: %s\n", animation_name, argv[3]);
                } else {
                    printf("Image file not found: %s\n", argv[3]);
                }
            } else if (ret == ESP_ERR_INVALID_ARG) {
                printf("No animations found in file: %s\n", argv[3]);
            }
        } else if (strcmp(argv[2], "list") == 0) {
            if (argc < 4) {
                printf("Usage: led matrix image list <filepath>\n");
                printf("Example: led matrix image list /sdcard/logo.json\n");
                return 1;
            }
            ret = matrix_led_list_animations(argv[3]);
            if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
                printf("Failed to list animations in file: %s\n", argv[3]);
            }
        } else {
            printf("Invalid image command\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "storage") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix storage <status|test>\n");
            return 1;
        }
        
        if (strcmp(argv[2], "status") == 0) {
            bool available = matrix_led_storage_available();
            printf("Storage Status:\n");
            printf("  SD Card: %s\n", available ? "Available (/sdcard)" : "Not available");
            if (available) {
                // 检查SD卡可用性（ESP-IDF下的简单检查）
                struct stat st;
                if (stat("/sdcard", &st) == 0) {
                    printf("  Mount Point: Accessible\n");
                } else {
                    printf("  Mount Point: Error accessing\n");
                }
            }
            printf("  NVS Backup: Always available\n");
        } else if (strcmp(argv[2], "testwrite") == 0) {
            // Test config file writing specifically
            printf("Testing configuration file write...\n");
            
            // Test paths with 8.3 filename format
            const char* test_paths[] = {
                "/sdcard/cfg.txt",
                "/sdcard/led/cfg.txt", 
                "/sdcard/cfg.json",
                "/sdcard/led/cfg.json",
                "/sdcard/config.txt"
            };
            
            for (int i = 0; i < 5; i++) {
                printf("Testing path: %s\n", test_paths[i]);
                esp_err_t ret = matrix_led_export_config(test_paths[i]);
                if (ret == ESP_OK) {
                    printf("✓ Config export SUCCESS: %s\n", test_paths[i]);
                    // Try to read it back
                    esp_err_t import_ret = matrix_led_import_config(test_paths[i]);
                    if (import_ret == ESP_OK) {
                        printf("✓ Config import SUCCESS: %s\n", test_paths[i]);
                    } else {
                        printf("✗ Config import FAILED: %s\n", test_paths[i]);
                    }
                    // Clean up
                    remove(test_paths[i]);
                    break;
                } else {
                    printf("✗ Config export FAILED: %s\n", test_paths[i]);
                }
            }
        } else if (strcmp(argv[2], "test") == 0) {
            // Test file creation with detailed debugging
            printf("Testing SD card filesystem...\n");
            
            // 1. Check mount point accessibility
            struct stat st;
            if (stat("/sdcard", &st) != 0) {
                printf("✗ Mount point not accessible (errno: %d - %s)\n", errno, strerror(errno));
                return 1;
            }
            printf("✓ Mount point accessible\n");
            
            // 2. Check if writable (try to create directory)
            errno = 0;  // Clear errno before operation
            if (mkdir("/sdcard/matrix_led", 0755) != 0) {
                if (errno == EEXIST) {
                    printf("✓ Directory creation: Already exists\n");
                } else {
                    printf("✗ Cannot create directory (errno: %d - %s)\n", errno, strerror(errno));
                    printf("  Trying alternative directory name...\n");
                    // Try creating with a simpler name
                    errno = 0;
                    if (mkdir("/sdcard/led", 0755) != 0 && errno != EEXIST) {
                        printf("✗ Alternative directory failed (errno: %d - %s)\n", errno, strerror(errno));
                    } else {
                        printf("✓ Alternative directory: OK (/sdcard/led)\n");
                    }
                }
            } else {
                printf("✓ Directory creation: OK\n");
            }
            
            // 3. Test basic file write
            printf("Testing file write operations...\n");
            
            // First try subdirectory if it exists/was created
            const char* test_paths[] = {
                "/sdcard/matrix_led/test.txt",
                "/sdcard/led/test.txt", 
                "/sdcard/test.txt"
            };
            
            bool write_success = false;
            const char* successful_path = NULL;
            
            for (int i = 0; i < 3 && !write_success; i++) {
                errno = 0;
                FILE *test_file = fopen(test_paths[i], "w");
                if (test_file != NULL) {
                    int bytes_written = fprintf(test_file, "Matrix LED test file - %lu\n", (unsigned long)esp_timer_get_time());
                    fflush(test_file);  // Force write to disk
                    fclose(test_file);
                    
                    printf("✓ File write: SUCCESS (%d bytes) at %s\n", bytes_written, test_paths[i]);
                    successful_path = test_paths[i];
                    write_success = true;
                    
                    // 4. Test file read back
                    test_file = fopen(test_paths[i], "r");
                    if (test_file != NULL) {
                        char buffer[128];
                        if (fgets(buffer, sizeof(buffer), test_file) != NULL) {
                            printf("✓ File read: SUCCESS\n");
                            printf("  Content: %s", buffer);
                        } else {
                            printf("✗ File read: No data\n");
                        }
                        fclose(test_file);
                        
                        // 5. Clean up
                        if (remove(test_paths[i]) == 0) {
                            printf("✓ File deletion: SUCCESS\n");
                        } else {
                            printf("⚠ File deletion: FAILED (errno: %d - %s)\n", errno, strerror(errno));
                        }
                    } else {
                        printf("✗ File read: Cannot open (errno: %d - %s)\n", errno, strerror(errno));
                    }
                } else {
                    printf("✗ File write at %s: FAILED (errno: %d - %s)\n", test_paths[i], errno, strerror(errno));
                }
            }
            
            if (!write_success) {
                printf("✗ All file write attempts failed\n");
            }
            
            // Check filesystem info using ESP-IDF function
            printf("\nFilesystem diagnostics:\n");
            uint64_t total_bytes = 0, free_bytes = 0;
            esp_err_t info_ret = esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes);
            if (info_ret == ESP_OK) {
                uint32_t total_mb = total_bytes / (1024 * 1024);
                uint32_t free_mb = free_bytes / (1024 * 1024);
                printf("  Total space: %lu MB\n", (unsigned long)total_mb);
                printf("  Free space: %lu MB\n", (unsigned long)free_mb);
                printf("  Used space: %lu MB (%.1f%%)\n", (unsigned long)(total_mb - free_mb), 
                       total_mb > 0 ? (float)(total_mb - free_mb) / total_mb * 100.0 : 0.0);
            } else {
                printf("  Cannot get filesystem info: %s\n", esp_err_to_name(info_ret));
            }
        } else {
            printf("Invalid storage command\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "draw") == 0) {
        if (argc < 3) {
            printf("Usage: led matrix draw <line|rect|circle> ...\n");
            return 1;
        }
        
        if (strcmp(argv[2], "line") == 0) {
            if (argc < 9) {
                printf("Usage: led matrix draw line <x0> <y0> <x1> <y1> <r> <g> <b>\n");
                return 1;
            }
            int x0 = atoi(argv[3]), y0 = atoi(argv[4]);
            int x1 = atoi(argv[5]), y1 = atoi(argv[6]);
            int r = atoi(argv[7]), g = atoi(argv[8]), b = atoi(argv[9]);
            
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                matrix_led_color_t color = {r, g, b};
                ret = matrix_led_draw_line(x0, y0, x1, y1, color);
                if (ret == ESP_OK) {
                    matrix_led_refresh();
                    printf("Line drawn from (%d,%d) to (%d,%d)\n", x0, y0, x1, y1);
                }
            }
        }
        else if (strcmp(argv[2], "rect") == 0) {
            if (argc < 9) {
                printf("Usage: led matrix draw rect <x> <y> <w> <h> <r> <g> <b> [fill]\n");
                return 1;
            }
            int x = atoi(argv[3]), y = atoi(argv[4]);
            int w = atoi(argv[5]), h = atoi(argv[6]);
            int r = atoi(argv[7]), g = atoi(argv[8]), b = atoi(argv[9]);
            bool filled = (argc >= 11 && strcmp(argv[10], "fill") == 0);
            
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                matrix_led_rect_t rect = {x, y, w, h};
                matrix_led_color_t color = {r, g, b};
                ret = matrix_led_draw_rect(&rect, color, filled);
                if (ret == ESP_OK) {
                    matrix_led_refresh();
                    printf("%s rectangle drawn at (%d,%d) size %dx%d\n", 
                           filled ? "Filled" : "Outline", x, y, w, h);
                }
            }
        }
        else if (strcmp(argv[2], "circle") == 0) {
            if (argc < 8) {
                printf("Usage: led matrix draw circle <x> <y> <radius> <r> <g> <b> [fill]\n");
                return 1;
            }
            int x = atoi(argv[3]), y = atoi(argv[4]);
            int radius = atoi(argv[5]);
            int r = atoi(argv[6]), g = atoi(argv[7]), b = atoi(argv[8]);
            bool filled = (argc >= 10 && strcmp(argv[9], "fill") == 0);
            
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                matrix_led_color_t color = {r, g, b};
                ret = matrix_led_draw_circle(x, y, radius, color, filled);
                if (ret == ESP_OK) {
                    matrix_led_refresh();
                    printf("%s circle drawn at (%d,%d) radius %d\n", 
                           filled ? "Filled" : "Outline", x, y, radius);
                }
            }
        }
    }
    else {
        printf("Unknown matrix LED command: %s\n", argv[1]);
        return 1;
    }

    if (ret != ESP_OK) {
        printf("Command failed: %s\n", esp_err_to_name(ret));
        return 1;
    }

    return 0;
}

static void matrix_led_register_console_commands(void)
{
    // Matrix LED作为led命令的子命令，不需要单独注册
    // led命令已经由touch_led组件注册，我们需要扩展它
    ESP_LOGI(TAG, "Matrix LED uses 'led matrix' commands (shared with touch LED)");
    ESP_LOGI(TAG, "Use 'led matrix anim rainbow' to test animations");
}

// ==================== 存储功能实现 ====================

/**
 * @brief 创建文件路径的目录结构
 * @param filepath 文件路径
 * @return ESP_OK成功，其他值失败
 */
static esp_err_t ensure_directory_exists(const char* filepath)
{
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 复制文件路径，因为我们需要修改字符串
    char *path_copy = strdup(filepath);
    if (path_copy == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 获取目录路径
    char *last_slash = strrchr(path_copy, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';  // 截断到目录部分
        
        // 检查目录是否已存在
        struct stat st;
        if (stat(path_copy, &st) == 0) {
            // 目录已存在
            free(path_copy);
            return ESP_OK;
        }
        
        // 目录不存在，递归创建父目录
        esp_err_t parent_ret = ensure_directory_exists(path_copy);
        if (parent_ret != ESP_OK) {
            free(path_copy);
            return parent_ret;
        }
        
        // 创建当前目录
        ESP_LOGI(TAG, "Creating directory: %s", path_copy);
        if (mkdir(path_copy, 0755) != 0) {
            ESP_LOGW(TAG, "Failed to create directory: %s (errno: %d - %s)", 
                     path_copy, errno, strerror(errno));
            free(path_copy);
            return ESP_FAIL;
        }
    }
    
    free(path_copy);
    return ESP_OK;
}

bool matrix_led_storage_available(void)
{
    // 检查SD卡是否挂载（通过检查/sdcard目录是否可访问）
    struct stat st;
    return (stat("/sdcard", &st) == 0 && S_ISDIR(st.st_mode));
}

esp_err_t matrix_led_export_config(const char* filepath)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!matrix_led_storage_available()) {
        ESP_LOGW(TAG, "SD card not available for config export");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 仅在有子目录时才尝试创建目录
    char *last_slash = strrchr(filepath, '/');
    if (last_slash != NULL && last_slash != filepath && strstr(filepath, "/sdcard/") == filepath) {
        // 检查是否不在根目录，如果不在根目录则尝试创建
        if (strncmp(filepath, "/sdcard/", 8) == 0 && strchr(filepath + 8, '/') != NULL) {
            ESP_LOGI(TAG, "Attempting to create directory structure for: %s", filepath);
            esp_err_t dir_ret = ensure_directory_exists(filepath);
            if (dir_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to create directory, please use root directory like /sdcard/config.json");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    // 创建JSON配置对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 添加基本配置
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "brightness", s_context.brightness);
    cJSON_AddNumberToObject(root, "mode", s_context.mode);
    cJSON_AddBoolToObject(root, "enabled", s_context.enabled);
    
    // 添加动画配置（如果有）
    if (s_context.animation.is_running) {
        cJSON *animation = cJSON_CreateObject();
        cJSON_AddStringToObject(animation, "name", s_context.animation.config.name);
        cJSON_AddNumberToObject(animation, "type", s_context.animation.type);
        cJSON_AddNumberToObject(animation, "speed", s_context.animation.config.speed);
        cJSON_AddBoolToObject(animation, "loop", s_context.animation.config.loop);
        cJSON_AddItemToObject(root, "animation", animation);
    }
    
    // 将JSON转换为字符串
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 调试：显示JSON内容
    ESP_LOGI(TAG, "Generated JSON (%zu bytes):", strlen(json_string));
    ESP_LOGI(TAG, "JSON content: %s", json_string);
    
    // 写入文件
    ESP_LOGI(TAG, "Attempting to write config to: %s", filepath);
    errno = 0;  // Clear errno before operation
    FILE *file = fopen(filepath, "w");
    if (file == NULL) {
        free(json_string);
        ESP_LOGE(TAG, "Failed to open file for writing: %s (errno: %d - %s)", 
                 filepath, errno, strerror(errno));
        
        // 尝试简单文本写入作为对比
        ESP_LOGI(TAG, "Trying simple text write to same path...");
        file = fopen(filepath, "w");
        if (file != NULL) {
            fprintf(file, "simple test\n");
            fclose(file);
            ESP_LOGI(TAG, "Simple text write succeeded");
            remove(filepath);  // Clean up
        } else {
            ESP_LOGE(TAG, "Simple text write also failed (errno: %d - %s)", errno, strerror(errno));
        }
        return ESP_FAIL;
    }
    
    size_t json_len = strlen(json_string);
    size_t written = fwrite(json_string, 1, json_len, file);
    
    // 强制刷新到磁盘
    if (fflush(file) != 0) {
        ESP_LOGW(TAG, "Failed to flush file buffer (errno: %d - %s)", errno, strerror(errno));
    }
    
    fclose(file);
    free(json_string);
    
    if (written == json_len) {
        ESP_LOGI(TAG, "Configuration exported successfully to: %s (%zu bytes)", filepath, written);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to write complete configuration to file (wrote %zu of %zu bytes)", 
                 written, json_len);
        return ESP_FAIL;
    }
}

esp_err_t matrix_led_import_config(const char* filepath)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "Configuration file not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取文件内容
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open configuration file: %s", filepath);
        return ESP_FAIL;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *json_string = malloc(file_size + 1);
    if (json_string == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(json_string, 1, file_size, file);
    fclose(file);
    json_string[read_size] = '\0';
    
    // 解析JSON
    cJSON *root = cJSON_Parse(json_string);
    free(json_string);
    
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse configuration JSON");
        return ESP_FAIL;
    }
    
    // 读取配置并应用
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(brightness)) {
        matrix_led_set_brightness((uint8_t)brightness->valueint);
    }
    
    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(enabled)) {
        matrix_led_set_enable(cJSON_IsTrue(enabled));
    }
    
    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    if (cJSON_IsNumber(mode)) {
        matrix_led_set_mode((matrix_led_mode_t)mode->valueint);
    }
    
    // 处理动画配置
    cJSON *animation = cJSON_GetObjectItem(root, "animation");
    if (animation != NULL) {
        cJSON *anim_type = cJSON_GetObjectItem(animation, "type");
        cJSON *anim_speed = cJSON_GetObjectItem(animation, "speed");
        
        if (cJSON_IsNumber(anim_type) && cJSON_IsNumber(anim_speed)) {
            matrix_led_animation_config_t config = {0};
            config.type = (matrix_led_animation_type_t)anim_type->valueint;
            config.speed = (uint8_t)anim_speed->valueint;
            config.loop = true;
            config.frame_delay_ms = 100 - config.speed;
            config.primary_color = MATRIX_LED_COLOR_BLUE;
            config.secondary_color = MATRIX_LED_COLOR_RED;
            
            cJSON *anim_name = cJSON_GetObjectItem(animation, "name");
            if (cJSON_IsString(anim_name)) {
                strncpy(config.name, anim_name->valuestring, MATRIX_LED_MAX_NAME_LEN - 1);
            }
            
            matrix_led_play_animation(config.type, &config);
        }
    }
    
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Configuration imported from: %s", filepath);
    return ESP_OK;
}

esp_err_t matrix_led_export_image(const char* filepath)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!matrix_led_storage_available()) {
        ESP_LOGW(TAG, "SD card not available for image export");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 确保目录存在
    esp_err_t dir_ret = ensure_directory_exists(filepath);
    if (dir_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create directory for: %s", filepath);
        return dir_ret;
    }
    
    // 停止动画以获取当前静态内容
    bool had_animation = s_context.animation.is_running;
    if (had_animation) {
        matrix_led_stop_animation();
    }
    
    // 创建兼容你的动画格式的JSON图像对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 创建animations数组
    cJSON *animations = cJSON_CreateArray();
    cJSON *animation = cJSON_CreateObject();
    
    // 添加动画名称（基于当前时间戳）
    char name_buffer[64];
    snprintf(name_buffer, sizeof(name_buffer), "Static_Image_%lu", (unsigned long)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(animation, "name", name_buffer);
    
    // 创建points数组，只包含非黑色像素
    cJSON *points = cJSON_CreateArray();
    for (int y = 0; y < MATRIX_LED_HEIGHT; y++) {
        for (int x = 0; x < MATRIX_LED_WIDTH; x++) {
            int index = y * MATRIX_LED_WIDTH + x;
            
            // 只导出非黑色像素（避免大量空白数据）
            if (s_context.pixel_buffer[index].r > 0 || 
                s_context.pixel_buffer[index].g > 0 || 
                s_context.pixel_buffer[index].b > 0) {
                
                cJSON *point = cJSON_CreateObject();
                cJSON_AddStringToObject(point, "type", "point");
                cJSON_AddNumberToObject(point, "x", x);
                cJSON_AddNumberToObject(point, "y", y);
                cJSON_AddNumberToObject(point, "r", s_context.pixel_buffer[index].r);
                cJSON_AddNumberToObject(point, "g", s_context.pixel_buffer[index].g);
                cJSON_AddNumberToObject(point, "b", s_context.pixel_buffer[index].b);
                cJSON_AddItemToArray(points, point);
            }
        }
    }
    
    cJSON_AddItemToObject(animation, "points", points);
    cJSON_AddItemToArray(animations, animation);
    cJSON_AddItemToObject(root, "animations", animations);
    
    // 将JSON转换为字符串
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 写入文件
    FILE *file = fopen(filepath, "w");
    if (file == NULL) {
        free(json_string);
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(json_string, 1, strlen(json_string), file);
    fclose(file);
    free(json_string);
    
    ESP_LOGI(TAG, "Image exported to: %s (%zu bytes)", filepath, written);
    return ESP_OK;
}

esp_err_t matrix_led_import_image(const char* filepath)
{
    return matrix_led_import_image_by_name(filepath, NULL);
}

esp_err_t matrix_led_import_image_by_name(const char* filepath, const char* animation_name)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "Image file not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取文件内容
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open image file: %s", filepath);
        return ESP_FAIL;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *json_string = malloc(file_size + 1);
    if (json_string == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(json_string, 1, file_size, file);
    fclose(file);
    json_string[read_size] = '\0';
    
    // 解析JSON
    cJSON *root = cJSON_Parse(json_string);
    free(json_string);
    
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse image JSON");
        return ESP_FAIL;
    }
    
    // 停止动画并清除显示
    matrix_led_stop_animation();
    matrix_led_clear();
    
    bool loaded_data = false;
    cJSON *target_animation = NULL;
    
    // 尝试加载新格式（animations数组）
    cJSON *animations = cJSON_GetObjectItem(root, "animations");
    if (cJSON_IsArray(animations) && cJSON_GetArraySize(animations) > 0) {
        
        if (animation_name == NULL) {
            // 如果没有指定名称，使用第一个动画
            target_animation = cJSON_GetArrayItem(animations, 0);
        } else {
            // 搜索指定名称的动画
            int animation_count = cJSON_GetArraySize(animations);
            for (int i = 0; i < animation_count; i++) {
                cJSON *animation = cJSON_GetArrayItem(animations, i);
                if (animation != NULL) {
                    cJSON *name = cJSON_GetObjectItem(animation, "name");
                    if (name && cJSON_IsString(name) && 
                        strcmp(name->valuestring, animation_name) == 0) {
                        target_animation = animation;
                        break;
                    }
                }
            }
            
            if (target_animation == NULL) {
                ESP_LOGW(TAG, "Animation '%s' not found in file", animation_name);
                cJSON_Delete(root);
                return ESP_ERR_NOT_FOUND;
            }
        }
        
        if (target_animation != NULL) {
            cJSON *points = cJSON_GetObjectItem(target_animation, "points");
            cJSON *name = cJSON_GetObjectItem(target_animation, "name");
            
            if (name && cJSON_IsString(name)) {
                ESP_LOGI(TAG, "Loading animation: %s", name->valuestring);
            }
            
            if (cJSON_IsArray(points)) {
                int point_count = cJSON_GetArraySize(points);
                ESP_LOGI(TAG, "Processing %d points", point_count);
                
                for (int i = 0; i < point_count; i++) {
                    cJSON *point = cJSON_GetArrayItem(points, i);
                    if (point != NULL) {
                        cJSON *x = cJSON_GetObjectItem(point, "x");
                        cJSON *y = cJSON_GetObjectItem(point, "y");
                        cJSON *r = cJSON_GetObjectItem(point, "r");
                        cJSON *g = cJSON_GetObjectItem(point, "g");
                        cJSON *b = cJSON_GetObjectItem(point, "b");
                        
                        if (cJSON_IsNumber(x) && cJSON_IsNumber(y) && 
                            cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
                            
                            int px = x->valueint;
                            int py = y->valueint;
                            
                            // 检查坐标范围
                            if (px >= 0 && px < MATRIX_LED_WIDTH && py >= 0 && py < MATRIX_LED_HEIGHT) {
                                int index = py * MATRIX_LED_WIDTH + px;
                                s_context.pixel_buffer[index].r = (uint8_t)r->valueint;
                                s_context.pixel_buffer[index].g = (uint8_t)g->valueint;
                                s_context.pixel_buffer[index].b = (uint8_t)b->valueint;
                            }
                        }
                    }
                }
                loaded_data = true;
            }
        }
    }
    
    // 如果新格式失败，尝试旧格式（向后兼容）
    if (!loaded_data && animation_name == NULL) {
        cJSON *width = cJSON_GetObjectItem(root, "width");
        cJSON *height = cJSON_GetObjectItem(root, "height");
        cJSON *pixels = cJSON_GetObjectItem(root, "pixels");
        
        if (cJSON_IsNumber(width) && cJSON_IsNumber(height) && cJSON_IsArray(pixels) &&
            width->valueint == MATRIX_LED_WIDTH && height->valueint == MATRIX_LED_HEIGHT) {
            
            int pixel_count = cJSON_GetArraySize(pixels);
            if (pixel_count == MATRIX_LED_COUNT) {
                ESP_LOGI(TAG, "Loading legacy format image");
                for (int i = 0; i < pixel_count; i++) {
                    cJSON *pixel = cJSON_GetArrayItem(pixels, i);
                    if (pixel != NULL) {
                        cJSON *r = cJSON_GetObjectItem(pixel, "r");
                        cJSON *g = cJSON_GetObjectItem(pixel, "g");
                        cJSON *b = cJSON_GetObjectItem(pixel, "b");
                        
                        if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
                            s_context.pixel_buffer[i].r = (uint8_t)r->valueint;
                            s_context.pixel_buffer[i].g = (uint8_t)g->valueint;
                            s_context.pixel_buffer[i].b = (uint8_t)b->valueint;
                        }
                    }
                }
                loaded_data = true;
            }
        }
        
        // 应用亮度设置（如果有）
        cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
        if (cJSON_IsNumber(brightness)) {
            matrix_led_set_brightness((uint8_t)brightness->valueint);
        }
    }
    
    if (!loaded_data) {
        if (animation_name != NULL) {
            ESP_LOGE(TAG, "Animation '%s' not found or no valid data", animation_name);
        } else {
            ESP_LOGE(TAG, "No valid image data found in JSON");
        }
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 刷新显示
    matrix_led_refresh();
    
    cJSON_Delete(root);
    if (animation_name) {
        ESP_LOGI(TAG, "Animation '%s' imported from: %s", animation_name, filepath);
    } else {
        ESP_LOGI(TAG, "Image imported from: %s", filepath);
    }
    return ESP_OK;
}

esp_err_t matrix_led_list_animations(const char* filepath)
{
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) != 0) {
        printf("Animation file not found: %s\n", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取文件内容
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        printf("Failed to open animation file: %s\n", filepath);
        return ESP_FAIL;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *json_string = malloc(file_size + 1);
    if (json_string == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(json_string, 1, file_size, file);
    fclose(file);
    json_string[read_size] = '\0';
    
    // 解析JSON
    cJSON *root = cJSON_Parse(json_string);
    free(json_string);
    
    if (root == NULL) {
        printf("Failed to parse JSON file: %s\n", filepath);
        return ESP_FAIL;
    }
    
    // 查找animations数组
    cJSON *animations = cJSON_GetObjectItem(root, "animations");
    if (!cJSON_IsArray(animations)) {
        printf("No animations array found in file: %s\n", filepath);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    int animation_count = cJSON_GetArraySize(animations);
    if (animation_count == 0) {
        printf("No animations found in file: %s\n", filepath);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    printf("Animations in file: %s\n", filepath);
    printf("Found %d animation(s):\n", animation_count);
    
    for (int i = 0; i < animation_count; i++) {
        cJSON *animation = cJSON_GetArrayItem(animations, i);
        if (animation != NULL) {
            cJSON *name = cJSON_GetObjectItem(animation, "name");
            cJSON *points = cJSON_GetObjectItem(animation, "points");
            
            const char* animation_name = "Unnamed";
            int point_count = 0;
            
            if (name && cJSON_IsString(name)) {
                animation_name = name->valuestring;
            }
            
            if (cJSON_IsArray(points)) {
                point_count = cJSON_GetArraySize(points);
            }
            
            printf("  [%d] %s (%d points)\n", i + 1, animation_name, point_count);
        }
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t matrix_led_export_animation(const char* animation_name, const char* filepath)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (animation_name == NULL || filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!matrix_led_storage_available()) {
        ESP_LOGW(TAG, "SD card not available for animation export");
        return ESP_ERR_INVALID_STATE;
    }
    
    // TODO: 实现自定义动画导出
    // 当前版本暂不支持自定义动画录制
    ESP_LOGW(TAG, "Custom animation export not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t matrix_led_import_animation(const char* filepath, const char* animation_name)
{
    if (!s_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (filepath == NULL || animation_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "Animation file not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    // TODO: 实现自定义动画导入
    // 当前版本暂不支持自定义动画播放
    ESP_LOGW(TAG, "Custom animation import not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}