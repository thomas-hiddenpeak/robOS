/**
 * @file test_matrix_led.c
 * @brief Matrix LED 测试程序
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "matrix_led.h"

static const char *TAG = "TEST_MATRIX_LED";

void test_matrix_led_basic(void)
{
    ESP_LOGI(TAG, "=== 开始Matrix LED基础测试 ===");
    
    // 测试初始化
    ESP_LOGI(TAG, "测试初始化...");
    esp_err_t ret = matrix_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "初始化成功");
    
    // 检查初始化状态
    bool is_init = matrix_led_is_initialized();
    ESP_LOGI(TAG, "初始化状态检查: %s", is_init ? "已初始化" : "未初始化");
    
    // 等待一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试设置单个像素
    ESP_LOGI(TAG, "测试设置像素...");
    matrix_led_color_t red = MATRIX_LED_COLOR_RED;
    ret = matrix_led_set_pixel(0, 0, red);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "设置像素成功");
        matrix_led_refresh();
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        ESP_LOGE(TAG, "设置像素失败: %s", esp_err_to_name(ret));
    }
    
    // 清空显示
    ESP_LOGI(TAG, "清空显示...");
    matrix_led_clear();
    matrix_led_refresh();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "=== Matrix LED基础测试完成 ===");
}

void test_matrix_led_animation(void)
{
    ESP_LOGI(TAG, "=== 开始Matrix LED动画测试 ===");
    
    // 确保已初始化
    if (!matrix_led_is_initialized()) {
        ESP_LOGE(TAG, "Matrix LED未初始化，无法测试动画");
        return;
    }
    
    // 测试彩虹动画
    ESP_LOGI(TAG, "测试彩虹动画...");
    matrix_led_animation_config_t config = {0};
    config.type = MATRIX_LED_ANIM_RAINBOW;
    config.speed = 50;
    config.loop = true;
    config.frame_delay_ms = 50;
    config.primary_color = MATRIX_LED_COLOR_BLUE;
    config.secondary_color = MATRIX_LED_COLOR_RED;
    strncpy(config.name, "test_rainbow", MATRIX_LED_MAX_NAME_LEN - 1);
    
    esp_err_t ret = matrix_led_play_animation(MATRIX_LED_ANIM_RAINBOW, &config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "彩虹动画启动成功");
        // 运行5秒
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // 停止动画
        ESP_LOGI(TAG, "停止动画...");
        matrix_led_stop_animation();
        ESP_LOGI(TAG, "动画已停止");
    } else {
        ESP_LOGE(TAG, "彩虹动画启动失败: %s", esp_err_to_name(ret));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试呼吸动画
    ESP_LOGI(TAG, "测试呼吸动画...");
    config.type = MATRIX_LED_ANIM_BREATHE;
    config.speed = 30;
    strncpy(config.name, "test_breathe", MATRIX_LED_MAX_NAME_LEN - 1);
    
    ret = matrix_led_play_animation(MATRIX_LED_ANIM_BREATHE, &config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "呼吸动画启动成功");
        vTaskDelay(pdMS_TO_TICKS(5000));
        matrix_led_stop_animation();
        ESP_LOGI(TAG, "呼吸动画已停止");
    } else {
        ESP_LOGE(TAG, "呼吸动画启动失败: %s", esp_err_to_name(ret));
    }
    
    // 清空显示
    matrix_led_clear();
    matrix_led_refresh();
    
    ESP_LOGI(TAG, "=== Matrix LED动画测试完成 ===");
}

void test_matrix_led_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Matrix LED测试任务开始");
    
    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 执行基础测试
    test_matrix_led_basic();
    
    // 等待一段时间
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 执行动画测试
    test_matrix_led_animation();
    
    ESP_LOGI(TAG, "Matrix LED测试任务完成");
    vTaskDelete(NULL);
}

void start_matrix_led_test(void)
{
    xTaskCreate(test_matrix_led_task, "matrix_led_test", 4096, NULL, 5, NULL);
}