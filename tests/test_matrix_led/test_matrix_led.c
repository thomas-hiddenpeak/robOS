/**
 * @file test_matrix_led.c
 * @brief Matrix LED 组件单元测试
 */

#include "unity.h"
#include "matrix_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "test_matrix_led";

// 测试夹具
void setUp(void)
{
    // 在每个测试前执行
}

void tearDown(void)
{
    // 在每个测试后执行
}

// ==================== 基础功能测试 ====================

void test_matrix_led_init_deinit(void)
{
    ESP_LOGI(TAG, "Testing matrix LED initialization and deinitialization");
    
    // 测试初始化
    esp_err_t ret = matrix_led_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 验证状态
    TEST_ASSERT_TRUE(matrix_led_is_enabled());
    TEST_ASSERT_EQUAL(MATRIX_LED_DEFAULT_BRIGHTNESS, matrix_led_get_brightness());
    TEST_ASSERT_EQUAL(MATRIX_LED_MODE_STATIC, matrix_led_get_mode());
    
    // 测试重复初始化
    ret = matrix_led_init();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    // 测试反初始化
    ret = matrix_led_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 验证反初始化后的状态
    TEST_ASSERT_FALSE(matrix_led_is_enabled());
    
    // 测试重复反初始化
    ret = matrix_led_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

void test_matrix_led_enable_disable(void)
{
    ESP_LOGI(TAG, "Testing matrix LED enable/disable");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    // 测试禁用
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_set_enable(false));
    TEST_ASSERT_FALSE(matrix_led_is_enabled());
    
    // 测试启用
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_set_enable(true));
    TEST_ASSERT_TRUE(matrix_led_is_enabled());
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

void test_matrix_led_status(void)
{
    ESP_LOGI(TAG, "Testing matrix LED status retrieval");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    matrix_led_status_t status;
    esp_err_t ret = matrix_led_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    TEST_ASSERT_TRUE(status.initialized);
    TEST_ASSERT_TRUE(status.enabled);
    TEST_ASSERT_EQUAL(MATRIX_LED_MODE_STATIC, status.mode);
    TEST_ASSERT_EQUAL(MATRIX_LED_DEFAULT_BRIGHTNESS, status.brightness);
    TEST_ASSERT_EQUAL(MATRIX_LED_COUNT, status.pixel_count);
    
    // 测试空指针
    ret = matrix_led_get_status(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

// ==================== 像素控制测试 ====================

void test_matrix_led_pixel_operations(void)
{
    ESP_LOGI(TAG, "Testing matrix LED pixel operations");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    // 测试设置像素
    matrix_led_color_t red = {255, 0, 0};
    esp_err_t ret = matrix_led_set_pixel(10, 15, red);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 测试获取像素
    matrix_led_color_t retrieved_color;
    ret = matrix_led_get_pixel(10, 15, &retrieved_color);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(red.r, retrieved_color.r);
    TEST_ASSERT_EQUAL(red.g, retrieved_color.g);
    TEST_ASSERT_EQUAL(red.b, retrieved_color.b);
    
    // 测试边界条件
    ret = matrix_led_set_pixel(MATRIX_LED_WIDTH, 0, red);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = matrix_led_set_pixel(0, MATRIX_LED_HEIGHT, red);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // 测试空指针
    ret = matrix_led_get_pixel(0, 0, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

void test_matrix_led_bulk_operations(void)
{
    ESP_LOGI(TAG, "Testing matrix LED bulk operations");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    // 测试批量设置像素
    matrix_led_pixel_t pixels[] = {
        {0, 0, {255, 0, 0}},
        {1, 1, {0, 255, 0}},
        {2, 2, {0, 0, 255}}
    };
    
    esp_err_t ret = matrix_led_set_pixels(pixels, 3);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 验证设置结果
    matrix_led_color_t color;
    matrix_led_get_pixel(0, 0, &color);
    TEST_ASSERT_EQUAL(255, color.r);
    TEST_ASSERT_EQUAL(0, color.g);
    TEST_ASSERT_EQUAL(0, color.b);
    
    // 测试清空
    ret = matrix_led_clear();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    matrix_led_get_pixel(0, 0, &color);
    TEST_ASSERT_EQUAL(0, color.r);
    TEST_ASSERT_EQUAL(0, color.g);
    TEST_ASSERT_EQUAL(0, color.b);
    
    // 测试填充
    matrix_led_color_t fill_color = {100, 150, 200};
    ret = matrix_led_fill(fill_color);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    matrix_led_get_pixel(5, 5, &color);
    TEST_ASSERT_EQUAL(fill_color.r, color.r);
    TEST_ASSERT_EQUAL(fill_color.g, color.g);
    TEST_ASSERT_EQUAL(fill_color.b, color.b);
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

// ==================== 亮度控制测试 ====================

void test_matrix_led_brightness(void)
{
    ESP_LOGI(TAG, "Testing matrix LED brightness control");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    // 测试设置亮度
    for (uint8_t brightness = 0; brightness <= 100; brightness += 25) {
        esp_err_t ret = matrix_led_set_brightness(brightness);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(brightness, matrix_led_get_brightness());
    }
    
    // 测试超出范围的亮度
    esp_err_t ret = matrix_led_set_brightness(101);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

// ==================== 图形绘制测试 ====================

void test_matrix_led_drawing(void)
{
    ESP_LOGI(TAG, "Testing matrix LED drawing functions");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    matrix_led_color_t white = {255, 255, 255};
    
    // 测试绘制直线
    esp_err_t ret = matrix_led_draw_line(0, 0, 10, 10, white);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 测试绘制矩形
    matrix_led_rect_t rect = {5, 5, 10, 8};
    ret = matrix_led_draw_rect(&rect, white, false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = matrix_led_draw_rect(&rect, white, true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 测试绘制圆形
    ret = matrix_led_draw_circle(16, 16, 8, white, false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = matrix_led_draw_circle(16, 16, 5, white, true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 测试空指针
    ret = matrix_led_draw_rect(NULL, white, false);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

// ==================== 模式和动画测试 ====================

void test_matrix_led_modes(void)
{
    ESP_LOGI(TAG, "Testing matrix LED display modes");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    // 测试模式切换
    matrix_led_mode_t modes[] = {
        MATRIX_LED_MODE_STATIC,
        MATRIX_LED_MODE_ANIMATION,
        MATRIX_LED_MODE_CUSTOM,
        MATRIX_LED_MODE_OFF
    };
    
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        esp_err_t ret = matrix_led_set_mode(modes[i]);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(modes[i], matrix_led_get_mode());
    }
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

void test_matrix_led_animations(void)
{
    ESP_LOGI(TAG, "Testing matrix LED animations");
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());
    
    // 测试播放彩虹动画
    esp_err_t ret = matrix_led_rainbow_gradient(50);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    vTaskDelay(pdMS_TO_TICKS(500));  // 让动画运行一段时间
    
    // 测试停止动画
    ret = matrix_led_stop_animation();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 测试呼吸动画
    matrix_led_color_t blue = {0, 0, 255};
    ret = matrix_led_breathe_effect(blue, 70);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ret = matrix_led_stop_animation();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 测试超出范围的速度
    ret = matrix_led_rainbow_gradient(101);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());
}

// ==================== 颜色工具测试 ====================

void test_matrix_led_color_tools(void)
{
    ESP_LOGI(TAG, "Testing matrix LED color utility functions");
    
    // 测试RGB转HSV
    matrix_led_color_t red = {255, 0, 0};
    matrix_led_hsv_t hsv;
    esp_err_t ret = matrix_led_rgb_to_hsv(red, &hsv);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, hsv.h);
    TEST_ASSERT_EQUAL(100, hsv.s);
    TEST_ASSERT_EQUAL(100, hsv.v);
    
    // 测试HSV转RGB
    matrix_led_hsv_t blue_hsv = {240, 100, 100};
    matrix_led_color_t rgb;
    ret = matrix_led_hsv_to_rgb(blue_hsv, &rgb);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, rgb.r);
    TEST_ASSERT_EQUAL(0, rgb.g);
    TEST_ASSERT_EQUAL(255, rgb.b);
    
    // 测试颜色插值\n    matrix_led_color_t color1 = {0, 0, 0};\n    matrix_led_color_t color2 = {255, 255, 255};\n    matrix_led_color_t result;\n    ret = matrix_led_color_interpolate(color1, color2, 0.5f, &result);\n    TEST_ASSERT_EQUAL(ESP_OK, ret);\n    TEST_ASSERT_UINT8_WITHIN(5, 127, result.r);\n    TEST_ASSERT_UINT8_WITHIN(5, 127, result.g);\n    TEST_ASSERT_UINT8_WITHIN(5, 127, result.b);\n    \n    // 测试亮度应用\n    matrix_led_color_t bright_color = {200, 150, 100};\n    ret = matrix_led_apply_brightness(bright_color, 50, &result);\n    TEST_ASSERT_EQUAL(ESP_OK, ret);\n    TEST_ASSERT_EQUAL(100, result.r);\n    TEST_ASSERT_EQUAL(75, result.g);\n    TEST_ASSERT_EQUAL(50, result.b);\n    \n    // 测试空指针\n    ret = matrix_led_rgb_to_hsv(red, NULL);\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);\n    \n    ret = matrix_led_hsv_to_rgb(blue_hsv, NULL);\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);\n}\n\n// ==================== 特效测试 ====================\n\nvoid test_matrix_led_effects(void)\n{\n    ESP_LOGI(TAG, \"Testing matrix LED effects\");\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());\n    \n    // 测试测试图案\n    esp_err_t ret = matrix_led_show_test_pattern();\n    TEST_ASSERT_EQUAL(ESP_OK, ret);\n    \n    vTaskDelay(pdMS_TO_TICKS(200));  // 让效果显示一段时间\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());\n}\n\n// ==================== 配置管理测试 ====================\n\nvoid test_matrix_led_config(void)\n{\n    ESP_LOGI(TAG, \"Testing matrix LED configuration management\");\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());\n    \n    // 修改一些设置\n    matrix_led_set_brightness(75);\n    matrix_led_set_mode(MATRIX_LED_MODE_ANIMATION);\n    matrix_led_set_enable(false);\n    \n    // 保存配置\n    esp_err_t ret = matrix_led_save_config();\n    TEST_ASSERT_EQUAL(ESP_OK, ret);\n    \n    // 重置为默认值\n    ret = matrix_led_reset_config();\n    TEST_ASSERT_EQUAL(ESP_OK, ret);\n    \n    TEST_ASSERT_EQUAL(MATRIX_LED_DEFAULT_BRIGHTNESS, matrix_led_get_brightness());\n    TEST_ASSERT_EQUAL(MATRIX_LED_MODE_STATIC, matrix_led_get_mode());\n    TEST_ASSERT_TRUE(matrix_led_is_enabled());\n    \n    // 加载之前保存的配置\n    ret = matrix_led_load_config();\n    TEST_ASSERT_EQUAL(ESP_OK, ret);\n    \n    TEST_ASSERT_EQUAL(75, matrix_led_get_brightness());\n    TEST_ASSERT_EQUAL(MATRIX_LED_MODE_ANIMATION, matrix_led_get_mode());\n    TEST_ASSERT_FALSE(matrix_led_is_enabled());\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());\n}\n\n// ==================== 错误条件测试 ====================\n\nvoid test_matrix_led_error_conditions(void)\n{\n    ESP_LOGI(TAG, \"Testing matrix LED error conditions\");\n    \n    // 测试未初始化状态下的调用\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, matrix_led_set_pixel(0, 0, MATRIX_LED_COLOR_RED));\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, matrix_led_clear());\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, matrix_led_set_brightness(50));\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, matrix_led_set_mode(MATRIX_LED_MODE_STATIC));\n    TEST_ASSERT_FALSE(matrix_led_is_enabled());\n    TEST_ASSERT_EQUAL(0, matrix_led_get_brightness());\n    \n    // 初始化后测试边界条件\n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());\n    \n    // 测试无效参数\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, matrix_led_set_pixels(NULL, 1));\n    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, matrix_led_color_interpolate(\n        MATRIX_LED_COLOR_RED, MATRIX_LED_COLOR_BLUE, 1.5f, NULL));\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());\n}\n\n// ==================== 性能测试 ====================\n\nvoid test_matrix_led_performance(void)\n{\n    ESP_LOGI(TAG, \"Testing matrix LED performance\");\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_init());\n    \n    // 测试大量像素设置的性能\n    TickType_t start_time = xTaskGetTickCount();\n    \n    matrix_led_color_t colors[] = {\n        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}\n    };\n    \n    for (int i = 0; i < 100; i++) {\n        for (uint8_t y = 0; y < MATRIX_LED_HEIGHT; y++) {\n            for (uint8_t x = 0; x < MATRIX_LED_WIDTH; x++) {\n                matrix_led_set_pixel(x, y, colors[i % 4]);\n            }\n        }\n        matrix_led_refresh();\n    }\n    \n    TickType_t end_time = xTaskGetTickCount();\n    uint32_t duration_ms = (end_time - start_time) * portTICK_PERIOD_MS;\n    \n    ESP_LOGI(TAG, \"Performance test completed in %lu ms\", duration_ms);\n    ESP_LOGI(TAG, \"Average frame time: %.2f ms\", duration_ms / 100.0f);\n    \n    // 性能不应该太差（每帧不超过100ms）\n    TEST_ASSERT_LESS_THAN(10000, duration_ms);  // 总时间不超过10秒\n    \n    TEST_ASSERT_EQUAL(ESP_OK, matrix_led_deinit());\n}\n\n// ==================== 测试运行器 ====================\n\nvoid app_main(void)\n{\n    ESP_LOGI(TAG, \"Starting Matrix LED component tests\");\n    \n    UNITY_BEGIN();\n    \n    // 基础功能测试\n    RUN_TEST(test_matrix_led_init_deinit);\n    RUN_TEST(test_matrix_led_enable_disable);\n    RUN_TEST(test_matrix_led_status);\n    \n    // 像素控制测试\n    RUN_TEST(test_matrix_led_pixel_operations);\n    RUN_TEST(test_matrix_led_bulk_operations);\n    \n    // 亮度控制测试\n    RUN_TEST(test_matrix_led_brightness);\n    \n    // 图形绘制测试\n    RUN_TEST(test_matrix_led_drawing);\n    \n    // 模式和动画测试\n    RUN_TEST(test_matrix_led_modes);\n    RUN_TEST(test_matrix_led_animations);\n    \n    // 颜色工具测试\n    RUN_TEST(test_matrix_led_color_tools);\n    \n    // 特效测试\n    RUN_TEST(test_matrix_led_effects);\n    \n    // 配置管理测试\n    RUN_TEST(test_matrix_led_config);\n    \n    // 错误条件测试\n    RUN_TEST(test_matrix_led_error_conditions);\n    \n    // 性能测试\n    RUN_TEST(test_matrix_led_performance);\n    \n    UNITY_END();\n    \n    ESP_LOGI(TAG, \"All Matrix LED tests completed\");\n}