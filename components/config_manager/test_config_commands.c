/**
 * @file test_config_commands.c
 * @brief Test program for configuration management commands
 * 
 * This file provides a simple test to verify that the config command
 * system compiles correctly and basic functionality works.
 */

#include "config_manager.h" 
#include "console_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CONFIG_TEST";

void test_config_commands_task(void *arg)
{
    ESP_LOGI(TAG, "Starting configuration command test");
    
    // Wait for system initialization
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Initialize console core (if not already done)
    esp_err_t ret = console_core_init(NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize console core: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // Register config commands
    ret = config_manager_register_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register config commands: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Configuration commands registered successfully");
    ESP_LOGI(TAG, "You can now use the 'config' command in the console");
    ESP_LOGI(TAG, "Try: config help");
    
    // Keep task running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

esp_err_t test_config_commands_init(void)
{
    BaseType_t ret = xTaskCreate(
        test_config_commands_task,
        "config_test",
        4096,
        NULL,
        5,
        NULL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create config test task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}