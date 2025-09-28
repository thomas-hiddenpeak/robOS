/**
 * @file test_console_core.c
 * @brief Unit tests for Console Core Component
 * 
 * @version 1.0.0
 * @date 2025-09-28
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "console_core.h"
#include "event_manager.h"
#include "hardware_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TEST_CONSOLE_CORE";

/* ============================================================================
 * Test Helper Functions
 * ============================================================================ */

/**
 * @brief Test command function
 */
static esp_err_t test_command_handler(int argc, char **argv)
{
    ESP_LOGI(TAG, "Test command executed with %d arguments", argc);
    for (int i = 0; i < argc; i++) {
        ESP_LOGI(TAG, "  arg[%d]: %s", i, argv[i]);
    }
    return ESP_OK;
}

/**
 * @brief Test command that returns error
 */
static esp_err_t test_error_command_handler(int argc, char **argv)
{
    ESP_LOGI(TAG, "Test error command executed");
    return ESP_ERR_INVALID_ARG;
}

/* ============================================================================
 * Test Setup and Teardown
 * ============================================================================ */

void setUp(void)
{
    // This function is called before each test
}

void tearDown(void)
{
    // This function is called after each test
    if (console_core_is_initialized()) {
        console_core_deinit();
    }
}

/* ============================================================================
 * Test Cases
 * ============================================================================ */

/**
 * @brief Test console core initialization and deinitialization
 */
void test_console_core_init_deinit(void)
{
    ESP_LOGI(TAG, "Testing console core initialization and deinitialization");
    
    // Test initialization with default config
    console_config_t config = console_get_default_config();
    esp_err_t ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(console_core_is_initialized());
    
    // Test double initialization
    ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    // Test deinitialization
    ret = console_core_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(console_core_is_initialized());
    
    // Test double deinitialization
    ret = console_core_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * @brief Test console core status reporting
 */
void test_console_core_status(void)
{
    ESP_LOGI(TAG, "Testing console core status reporting");
    
    console_config_t config = console_get_default_config();
    esp_err_t ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    console_status_t status;
    ret = console_core_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    TEST_ASSERT_TRUE(status.initialized);
    TEST_ASSERT_EQUAL(config.uart_port, status.uart_port);
    TEST_ASSERT_EQUAL(config.baud_rate, status.baud_rate);
    TEST_ASSERT_GREATER_THAN_UINT32(0, status.commands_count);  // Built-in commands should be registered
    
    console_core_deinit();
}

/**
 * @brief Test command registration and unregistration
 */
void test_console_command_registration(void)
{
    ESP_LOGI(TAG, "Testing command registration and unregistration");
    
    console_config_t config = console_get_default_config();
    esp_err_t ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get initial command count
    console_status_t status;
    console_core_get_status(&status);
    uint32_t initial_count = status.commands_count;
    
    // Register a test command
    console_cmd_t test_cmd = {
        .command = "test",
        .help = "test - A test command",
        .hint = NULL,
        .func = test_command_handler,
        .min_args = 0,
        .max_args = 2
    };
    
    ret = console_register_command(&test_cmd);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check command count increased
    console_core_get_status(&status);
    TEST_ASSERT_EQUAL(initial_count + 1, status.commands_count);
    
    // Test duplicate registration
    ret = console_register_command(&test_cmd);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Unregister the command
    ret = console_unregister_command("test");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check command count decreased
    console_core_get_status(&status);
    TEST_ASSERT_EQUAL(initial_count, status.commands_count);
    
    // Test unregistering non-existent command
    ret = console_unregister_command("nonexistent");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
    
    console_core_deinit();
}

/**
 * @brief Test command execution
 */
void test_console_command_execution(void)
{
    ESP_LOGI(TAG, "Testing command execution");
    
    console_config_t config = console_get_default_config();
    esp_err_t ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Register test commands
    console_cmd_t test_cmd = {
        .command = "test",
        .help = "test [arg1] [arg2] - A test command",
        .hint = NULL,
        .func = test_command_handler,
        .min_args = 0,
        .max_args = 2
    };
    
    console_cmd_t error_cmd = {
        .command = "error",
        .help = "error - A command that returns error",
        .hint = NULL,
        .func = test_error_command_handler,
        .min_args = 0,
        .max_args = 0
    };
    
    ret = console_register_command(&test_cmd);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = console_register_command(&error_cmd);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test valid command execution
    ret = console_execute_command("test");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = console_execute_command("test arg1");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = console_execute_command("test arg1 arg2");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test command with too many arguments
    ret = console_execute_command("test arg1 arg2 arg3");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test non-existent command
    ret = console_execute_command("nonexistent");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
    
    // Test command that returns error
    ret = console_execute_command("error");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test empty command
    ret = console_execute_command("");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    console_core_deinit();
}

/**
 * @brief Test built-in commands
 */
void test_console_builtin_commands(void)
{
    ESP_LOGI(TAG, "Testing built-in commands");
    
    console_config_t config = console_get_default_config();
    esp_err_t ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test help command
    ret = console_execute_command("help");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test help for specific command
    ret = console_execute_command("help version");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test help for non-existent command
    ret = console_execute_command("help nonexistent");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
    
    // Test version command
    ret = console_execute_command("version");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test clear command
    ret = console_execute_command("clear");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test status command
    ret = console_execute_command("status");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test history command (initially empty)
    ret = console_execute_command("history");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    console_core_deinit();
}

/**
 * @brief Test console prompt functionality
 */
void test_console_prompt(void)
{
    ESP_LOGI(TAG, "Testing console prompt functionality");
    
    console_config_t config = console_get_default_config();
    esp_err_t ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test default prompt
    const char* prompt = console_get_prompt();
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_EQUAL_STRING("robOS> ", prompt);
    
    // Test setting custom prompt
    ret = console_set_prompt("test> ");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    prompt = console_get_prompt();
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_EQUAL_STRING("test> ", prompt);
    
    console_core_deinit();
}

/**
 * @brief Test console configuration validation
 */
void test_console_configuration(void)
{
    ESP_LOGI(TAG, "Testing console configuration validation");
    
    // Test NULL configuration
    esp_err_t ret = console_core_init(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test valid configuration
    console_config_t config = console_get_default_config();
    ret = console_core_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify configuration was applied
    console_status_t status;
    console_core_get_status(&status);
    TEST_ASSERT_EQUAL(config.uart_port, status.uart_port);
    TEST_ASSERT_EQUAL(config.baud_rate, status.baud_rate);
    
    console_core_deinit();
}

/**
 * @brief Test error conditions
 */
void test_console_error_conditions(void)
{
    ESP_LOGI(TAG, "Testing error conditions");
    
    // Test operations without initialization
    console_status_t status;
    esp_err_t ret = console_core_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = console_execute_command("test");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = console_set_prompt("test> ");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    const char* prompt = console_get_prompt();
    TEST_ASSERT_NULL(prompt);
    
    // Test invalid arguments
    console_config_t config = console_get_default_config();
    console_core_init(&config);
    
    ret = console_core_get_status(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = console_register_command(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = console_unregister_command(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = console_execute_command(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = console_set_prompt(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    console_core_deinit();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Console Core Unit Tests");
    
    // Initialize Unity test framework
    UNITY_BEGIN();
    
    // Run test cases
    RUN_TEST(test_console_core_init_deinit);
    RUN_TEST(test_console_core_status);
    RUN_TEST(test_console_command_registration);
    RUN_TEST(test_console_command_execution);
    RUN_TEST(test_console_builtin_commands);
    RUN_TEST(test_console_prompt);
    RUN_TEST(test_console_configuration);
    RUN_TEST(test_console_error_conditions);
    
    // Finish tests
    UNITY_END();
    
    ESP_LOGI(TAG, "Console Core Unit Tests Completed");
}