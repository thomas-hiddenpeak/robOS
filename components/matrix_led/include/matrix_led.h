/**
 * @file matrix_led.h
 * @brief Matrix LED 控制组件 - 32x32 WS2812 LED矩阵控制
 * 
 * 这个组件提供了对32x32 WS2812 LED矩阵的完整控制功能，包括：
 * - 单个像素控制
 * - 图形绘制（点、线、矩形、圆形等）
 * - 动画播放和管理
 * - 亮度控制和色彩校正
 * - 配置持久化
 * - 事件驱动的状态管理
 * 
 * 硬件规格：
 * - GPIO: 9
 * - 矩阵尺寸: 32x32 (1024个LED)
 * - LED类型: WS2812 (GRB格式)
 * - 驱动方式: RMT硬件驱动
 * - 颜色深度: 24位RGB
 */

#ifndef MATRIX_LED_H
#define MATRIX_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 事件定义 ====================

/**
 * @brief Matrix LED 事件基础
 */
ESP_EVENT_DECLARE_BASE(MATRIX_LED_EVENTS);

// ==================== 常量定义 ====================

#define MATRIX_LED_WIDTH         32                                          ///< LED矩阵宽度
#define MATRIX_LED_HEIGHT        32                                          ///< LED矩阵高度
#define MATRIX_LED_COUNT         (MATRIX_LED_WIDTH * MATRIX_LED_HEIGHT)     ///< LED总数量 (1024)
#define MATRIX_LED_GPIO          9                                           ///< GPIO引脚

#define MATRIX_LED_MAX_BRIGHTNESS    100                                     ///< 最大亮度百分比
#define MATRIX_LED_DEFAULT_BRIGHTNESS 50                                     ///< 默认亮度
#define MATRIX_LED_RMT_RESOLUTION    10000000                               ///< RMT分辨率 (10MHz)

#define MATRIX_LED_MAX_ANIMATIONS    16                                      ///< 最大动画数量
#define MATRIX_LED_MAX_NAME_LEN      32                                      ///< 动画名称最大长度

// ==================== 类型定义 ====================

/**
 * @brief RGB颜色结构体
 */
typedef struct {
    uint8_t r;          ///< 红色分量 (0-255)
    uint8_t g;          ///< 绿色分量 (0-255)
    uint8_t b;          ///< 蓝色分量 (0-255)
} matrix_led_color_t;

/**
 * @brief HSV颜色结构体
 */
typedef struct {
    uint16_t h;         ///< 色相 (0-359度)
    uint8_t s;          ///< 饱和度 (0-100%)
    uint8_t v;          ///< 亮度值 (0-100%)
} matrix_led_hsv_t;

/**
 * @brief 像素点结构体
 */
typedef struct {
    uint8_t x;          ///< X坐标 (0-31)
    uint8_t y;          ///< Y坐标 (0-31)
    matrix_led_color_t color;  ///< 像素颜色
} matrix_led_pixel_t;

/**
 * @brief 矩形结构体
 */
typedef struct {
    uint8_t x;          ///< 左上角X坐标
    uint8_t y;          ///< 左上角Y坐标
    uint8_t width;      ///< 宽度
    uint8_t height;     ///< 高度
} matrix_led_rect_t;

/**
 * @brief 显示模式枚举
 */
typedef enum {
    MATRIX_LED_MODE_STATIC = 0,     ///< 静态显示模式
    MATRIX_LED_MODE_ANIMATION,      ///< 动画播放模式
    MATRIX_LED_MODE_CUSTOM,         ///< 自定义模式
    MATRIX_LED_MODE_OFF             ///< 关闭模式
} matrix_led_mode_t;

/**
 * @brief 动画类型枚举
 */
typedef enum {
    MATRIX_LED_ANIM_STATIC = 0,     ///< 静态图案
    MATRIX_LED_ANIM_RAINBOW,        ///< 彩虹动画
    MATRIX_LED_ANIM_WAVE,           ///< 波浪动画
    MATRIX_LED_ANIM_BREATHE,        ///< 呼吸动画
    MATRIX_LED_ANIM_ROTATE,         ///< 旋转动画
    MATRIX_LED_ANIM_FADE,           ///< 渐变动画
    MATRIX_LED_ANIM_CUSTOM          ///< 自定义动画
} matrix_led_animation_type_t;

/**
 * @brief 动画配置结构体
 */
typedef struct {
    char name[MATRIX_LED_MAX_NAME_LEN];      ///< 动画名称
    matrix_led_animation_type_t type;         ///< 动画类型
    uint16_t duration_ms;                     ///< 动画持续时间 (毫秒)
    uint16_t frame_delay_ms;                  ///< 帧间延迟 (毫秒)
    bool loop;                                ///< 是否循环播放
    matrix_led_color_t primary_color;        ///< 主颜色
    matrix_led_color_t secondary_color;      ///< 辅助颜色
    uint8_t speed;                            ///< 动画速度 (0-100)
    void* custom_data;                        ///< 自定义数据指针
} matrix_led_animation_config_t;

/**
 * @brief 矩阵LED状态结构体
 */
typedef struct {
    bool initialized;                         ///< 是否已初始化
    bool enabled;                             ///< 是否启用
    matrix_led_mode_t mode;                   ///< 当前显示模式
    uint8_t brightness;                       ///< 当前亮度 (0-100)
    char current_animation[MATRIX_LED_MAX_NAME_LEN];  ///< 当前动画名称
    uint32_t pixel_count;                     ///< 像素总数
    uint32_t frame_count;                     ///< 帧计数器
} matrix_led_status_t;

/**
 * @brief 事件类型枚举
 */
typedef enum {
    MATRIX_LED_EVENT_INITIALIZED = 0,         ///< 初始化完成事件
    MATRIX_LED_EVENT_MODE_CHANGED,            ///< 模式变更事件
    MATRIX_LED_EVENT_BRIGHTNESS_CHANGED,      ///< 亮度变更事件
    MATRIX_LED_EVENT_ANIMATION_STARTED,       ///< 动画开始事件
    MATRIX_LED_EVENT_ANIMATION_STOPPED,       ///< 动画停止事件
    MATRIX_LED_EVENT_ANIMATION_COMPLETED,     ///< 动画完成事件
    MATRIX_LED_EVENT_ERROR                    ///< 错误事件
} matrix_led_event_type_t;

/**
 * @brief 事件数据结构体
 */
typedef struct {
    matrix_led_event_type_t type;             ///< 事件类型
    union {
        struct {
            matrix_led_mode_t old_mode;       ///< 旧模式
            matrix_led_mode_t new_mode;       ///< 新模式
        } mode_change;
        struct {
            uint8_t old_brightness;           ///< 旧亮度
            uint8_t new_brightness;           ///< 新亮度
        } brightness_change;
        struct {
            char animation_name[MATRIX_LED_MAX_NAME_LEN];  ///< 动画名称
        } animation;
        struct {
            esp_err_t error_code;             ///< 错误代码
            char description[64];             ///< 错误描述
        } error;
    } data;
} matrix_led_event_data_t;

// ==================== 预定义颜色常量 ====================

extern const matrix_led_color_t MATRIX_LED_COLOR_BLACK;
extern const matrix_led_color_t MATRIX_LED_COLOR_WHITE;
extern const matrix_led_color_t MATRIX_LED_COLOR_RED;
extern const matrix_led_color_t MATRIX_LED_COLOR_GREEN;
extern const matrix_led_color_t MATRIX_LED_COLOR_BLUE;
extern const matrix_led_color_t MATRIX_LED_COLOR_YELLOW;
extern const matrix_led_color_t MATRIX_LED_COLOR_CYAN;
extern const matrix_led_color_t MATRIX_LED_COLOR_MAGENTA;
extern const matrix_led_color_t MATRIX_LED_COLOR_ORANGE;
extern const matrix_led_color_t MATRIX_LED_COLOR_PURPLE;

// ==================== 核心API函数 ====================

/**
 * @brief 初始化Matrix LED组件
 * 
 * @return 
 *     - ESP_OK: 初始化成功
 *     - ESP_ERR_INVALID_STATE: 组件已经初始化
 *     - ESP_ERR_NO_MEM: 内存不足
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t matrix_led_init(void);

/**
 * @brief 反初始化Matrix LED组件
 * 
 * @return 
 *     - ESP_OK: 反初始化成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_deinit(void);

/**
 * @brief 启用/禁用Matrix LED
 * 
 * @param enable true为启用，false为禁用
 * @return 
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_set_enable(bool enable);

/**
 * @brief 获取Matrix LED启用状态
 * 
 * @return true为启用，false为禁用
 */
bool matrix_led_is_enabled(void);

/**
 * @brief 获取Matrix LED状态信息
 * 
 * @param status 状态信息结构体指针
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_get_status(matrix_led_status_t* status);

// ==================== 像素控制API ====================

/**
 * @brief 设置单个像素颜色
 * 
 * @param x X坐标 (0-31)
 * @param y Y坐标 (0-31)
 * @param color 像素颜色
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 坐标超出范围
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_set_pixel(uint8_t x, uint8_t y, matrix_led_color_t color);

/**
 * @brief 获取单个像素颜色
 * 
 * @param x X坐标 (0-31)
 * @param y Y坐标 (0-31)
 * @param color 输出颜色指针
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 坐标超出范围或指针为空
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_get_pixel(uint8_t x, uint8_t y, matrix_led_color_t* color);

/**
 * @brief 设置多个像素颜色
 * 
 * @param pixels 像素数组
 * @param count 像素数量
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_set_pixels(const matrix_led_pixel_t* pixels, size_t count);

/**
 * @brief 清空所有像素（设为黑色）
 * 
 * @return 
 *     - ESP_OK: 清空成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_clear(void);

/**
 * @brief 填充整个矩阵为指定颜色
 * 
 * @param color 填充颜色
 * @return 
 *     - ESP_OK: 填充成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_fill(matrix_led_color_t color);

/**
 * @brief 刷新显示（将缓冲区内容输出到LED）
 * 
 * @return 
 *     - ESP_OK: 刷新成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_refresh(void);

// ==================== 图形绘制API ====================

/**
 * @brief 绘制直线
 * 
 * @param x0 起点X坐标
 * @param y0 起点Y坐标
 * @param x1 终点X坐标
 * @param y1 终点Y坐标
 * @param color 线条颜色
 * @return 
 *     - ESP_OK: 绘制成功
 *     - ESP_ERR_INVALID_ARG: 坐标超出范围
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, matrix_led_color_t color);

/**
 * @brief 绘制矩形
 * 
 * @param rect 矩形结构体
 * @param color 边框颜色
 * @param filled 是否填充
 * @return 
 *     - ESP_OK: 绘制成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_draw_rect(const matrix_led_rect_t* rect, matrix_led_color_t color, bool filled);

/**
 * @brief 绘制圆形
 * 
 * @param center_x 圆心X坐标
 * @param center_y 圆心Y坐标
 * @param radius 半径
 * @param color 圆形颜色
 * @param filled 是否填充
 * @return 
 *     - ESP_OK: 绘制成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_draw_circle(uint8_t center_x, uint8_t center_y, uint8_t radius, matrix_led_color_t color, bool filled);

/**
 * @brief 绘制文本（简单字符）
 * 
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param text 文本内容
 * @param color 文本颜色
 * @return 
 *     - ESP_OK: 绘制成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_draw_text(uint8_t x, uint8_t y, const char* text, matrix_led_color_t color);

// ==================== 亮度控制API ====================

/**
 * @brief 设置矩阵亮度
 * 
 * @param brightness 亮度值 (0-100)
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 亮度值超出范围
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_set_brightness(uint8_t brightness);

/**
 * @brief 获取当前亮度
 * 
 * @return 当前亮度值 (0-100)，如果组件未初始化返回0
 */
uint8_t matrix_led_get_brightness(void);

// ==================== 动画控制API ====================

/**
 * @brief 设置显示模式
 * 
 * @param mode 显示模式
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 模式无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_set_mode(matrix_led_mode_t mode);

/**
 * @brief 获取当前显示模式
 * 
 * @return 当前显示模式
 */
matrix_led_mode_t matrix_led_get_mode(void);

/**
 * @brief 播放预定义动画
 * 
 * @param animation_type 动画类型
 * @param config 动画配置（可选，传NULL使用默认配置）
 * @return 
 *     - ESP_OK: 播放成功
 *     - ESP_ERR_INVALID_ARG: 动画类型无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_play_animation(matrix_led_animation_type_t animation_type, const matrix_led_animation_config_t* config);

/**
 * @brief 停止当前动画
 * 
 * @return 
 *     - ESP_OK: 停止成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化或无动画在播放
 */
esp_err_t matrix_led_stop_animation(void);

/**
 * @brief 加载自定义动画从文件
 * 
 * @param filename 动画文件名
 * @param animation_name 动画名称
 * @return 
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_NOT_FOUND: 文件不存在
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_load_animation_from_file(const char* filename, const char* animation_name);

/**
 * @brief 播放已加载的自定义动画
 * 
 * @param animation_name 动画名称
 * @return 
 *     - ESP_OK: 播放成功
 *     - ESP_ERR_NOT_FOUND: 动画不存在
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_play_custom_animation(const char* animation_name);

// ==================== 特效API ====================

/**
 * @brief 显示测试图案
 * 
 * @return 
 *     - ESP_OK: 显示成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_show_test_pattern(void);

/**
 * @brief 创建彩虹渐变效果
 * 
 * @param speed 渐变速度 (0-100)
 * @return 
 *     - ESP_OK: 创建成功
 *     - ESP_ERR_INVALID_ARG: 速度参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_rainbow_gradient(uint8_t speed);

/**
 * @brief 创建呼吸效果
 * 
 * @param color 呼吸颜色
 * @param speed 呼吸速度 (0-100)
 * @return 
 *     - ESP_OK: 创建成功
 *     - ESP_ERR_INVALID_ARG: 速度参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_breathe_effect(matrix_led_color_t color, uint8_t speed);

// ==================== 颜色工具API ====================

/**
 * @brief RGB转HSV
 * 
 * @param rgb RGB颜色
 * @param hsv 输出HSV颜色指针
 * @return 
 *     - ESP_OK: 转换成功
 *     - ESP_ERR_INVALID_ARG: 指针为空
 */
esp_err_t matrix_led_rgb_to_hsv(matrix_led_color_t rgb, matrix_led_hsv_t* hsv);

/**
 * @brief HSV转RGB
 * 
 * @param hsv HSV颜色
 * @param rgb 输出RGB颜色指针
 * @return 
 *     - ESP_OK: 转换成功
 *     - ESP_ERR_INVALID_ARG: 指针为空
 */
esp_err_t matrix_led_hsv_to_rgb(matrix_led_hsv_t hsv, matrix_led_color_t* rgb);

/**
 * @brief 颜色插值计算
 * 
 * @param color1 起始颜色
 * @param color2 结束颜色
 * @param ratio 插值比例 (0.0-1.0)
 * @param result 输出颜色指针
 * @return 
 *     - ESP_OK: 计算成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t matrix_led_color_interpolate(matrix_led_color_t color1, matrix_led_color_t color2, float ratio, matrix_led_color_t* result);

/**
 * @brief 应用亮度到颜色
 * 
 * @param color 原始颜色
 * @param brightness 亮度 (0-100)
 * @param result 输出颜色指针
 * @return 
 *     - ESP_OK: 应用成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t matrix_led_apply_brightness(matrix_led_color_t color, uint8_t brightness, matrix_led_color_t* result);

// ==================== 配置管理API ====================

/**
 * @brief 保存配置到NVS
 * 
 * @return 
 *     - ESP_OK: 保存成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 *     - ESP_FAIL: 保存失败
 */
esp_err_t matrix_led_save_config(void);

/**
 * @brief 从NVS加载配置
 * 
 * @return 
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_NOT_FOUND: 配置不存在
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_load_config(void);

/**
 * @brief 重置配置为默认值
 * 
 * @return 
 *     - ESP_OK: 重置成功
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 */
esp_err_t matrix_led_reset_config(void);

// ==================== 存储功能API ====================

/**
 * @brief 导出配置到SD卡文件
 * 
 * @param filepath SD卡上的文件路径 (例如: "/sdcard/matrix_config.json")
 * @return 
 *     - ESP_OK: 导出成功
 *     - ESP_ERR_INVALID_ARG: 文件路径无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化或SD卡不可用
 *     - ESP_FAIL: 文件写入失败
 */
esp_err_t matrix_led_export_config(const char* filepath);

/**
 * @brief 从SD卡文件导入配置
 * 
 * @param filepath SD卡上的文件路径 (例如: "/sdcard/matrix_config.json")
 * @return 
 *     - ESP_OK: 导入成功
 *     - ESP_ERR_INVALID_ARG: 文件路径无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 *     - ESP_ERR_NOT_FOUND: 文件不存在
 *     - ESP_FAIL: 文件读取或解析失败
 */
esp_err_t matrix_led_import_config(const char* filepath);

/**
 * @brief 保存当前静态内容到SD卡
 * 
 * @param filepath SD卡上的文件路径 (例如: "/sdcard/matrix_image.json")
 * @return 
 *     - ESP_OK: 保存成功
 *     - ESP_ERR_INVALID_ARG: 文件路径无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化或SD卡不可用
 *     - ESP_FAIL: 文件写入失败
 */
esp_err_t matrix_led_export_image(const char* filepath);

/**
 * @brief 从SD卡加载静态图像内容
 * 
 * @param filepath SD卡上的文件路径 (例如: "/sdcard/matrix_image.json")
 * @return 
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_INVALID_ARG: 文件路径无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 *     - ESP_ERR_NOT_FOUND: 文件不存在
 *     - ESP_FAIL: 文件读取或解析失败
 */
esp_err_t matrix_led_import_image(const char* filepath);

/**
 * @brief 从JSON文件按名称加载图像到LED矩阵
 * 
 * 支持按动画名称选择加载JSON文件中的特定动画，如果animation_name为NULL则加载第一个动画。
 * 
 * @param filepath JSON文件路径
 * @param animation_name 动画名称，为NULL时加载第一个动画
 * @return 
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_INVALID_ARG: 参数无效或文件中无动画
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 *     - ESP_ERR_NOT_FOUND: 文件不存在或指定动画不存在
 *     - ESP_FAIL: 文件读取或解析失败
 */
esp_err_t matrix_led_import_image_by_name(const char* filepath, const char* animation_name);

/**
 * @brief 列出JSON文件中的所有动画名称
 * 
 * @param filepath JSON文件路径
 * @return 
 *     - ESP_OK: 列出成功
 *     - ESP_ERR_INVALID_ARG: 文件路径无效
 *     - ESP_ERR_NOT_FOUND: 文件不存在
 *     - ESP_FAIL: 文件读取或解析失败
 */
esp_err_t matrix_led_list_animations(const char* filepath);

/**
 * @brief 保存自定义动画到SD卡
 * 
 * @param animation_name 动画名称
 * @param filepath SD卡上的文件路径 (例如: "/sdcard/animations/custom.bin")
 * @return 
 *     - ESP_OK: 保存成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化或SD卡不可用
 *     - ESP_FAIL: 文件写入失败
 */
esp_err_t matrix_led_export_animation(const char* animation_name, const char* filepath);

/**
 * @brief 从SD卡加载自定义动画
 * 
 * @param filepath SD卡上的文件路径 (例如: "/sdcard/animations/custom.bin")
 * @param animation_name 加载后的动画名称
 * @return 
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 组件未初始化
 *     - ESP_ERR_NOT_FOUND: 文件不存在
 *     - ESP_FAIL: 文件读取或解析失败
 */
esp_err_t matrix_led_import_animation(const char* filepath, const char* animation_name);

/**
 * @brief 检查SD卡存储是否可用
 * 
 * @return true如果SD卡可用，false如果不可用
 */
bool matrix_led_storage_available(void);

// ==================== 控制台集成API ====================

/**
 * @brief 检查Matrix LED是否已初始化
 * 
 * @return true如果已初始化，false如果未初始化
 */
bool matrix_led_is_initialized(void);

/**
 * @brief Matrix LED控制台命令处理器
 * 
 * @param argc 参数数量
 * @param argv 参数数组
 * @return 0表示成功，非0表示错误
 */
int matrix_led_cmd_handler(int argc, char **argv);

// ==================== 测试函数 ====================

/**
 * @brief 启动Matrix LED测试
 */
void start_matrix_led_test(void);

#ifdef __cplusplus
}
#endif

#endif // MATRIX_LED_H