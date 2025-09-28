/**
 * @file test_config_manager_simple.c
 * @brief Simplified Unit tests for config_manager component
 * 
 * This is a simplified version of the config manager tests with only the most essential tests.
 * 
 * @author robOS Team
 * @date 2025-09-28
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "config_manager.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ============================================================================
 * Test Constants and Macros
 * ============================================================================ */

#define TEST_NAMESPACE      "test_ns"
#define TEST_STRING_VALUE   "Hello, robOS!"

/* ============================================================================
 * Test Helper Functions
 * ============================================================================ */

/**
 * @brief Setup function called before each test
 */
void setUp(void)
{
    // Initialize NVS flash for testing
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clear any existing configuration
    if (config_manager_is_initialized()) {
        config_manager_deinit();
    }
}

/**
 * @brief Teardown function called after each test
 */
void tearDown(void)
{
    // Clean up after test
    if (config_manager_is_initialized()) {
        config_manager_clear_namespace(TEST_NAMESPACE);
        config_manager_deinit();
    }
}

/* ============================================================================
 * Basic Tests
 * ============================================================================ */

/**
 * @brief Test config manager initialization
 */
void test_config_manager_init(void)
{
    esp_err_t ret = config_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(config_manager_is_initialized());
}

/**
 * @brief Test uint32_t operations
 */
void test_config_manager_uint32(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t write_val = 0x12345678;
    uint32_t read_val = 0;
    
    // Test set
    esp_err_t ret = config_manager_set(TEST_NAMESPACE, "u32_test", CONFIG_TYPE_UINT32, 
                                      &write_val, sizeof(write_val));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test get
    ret = config_manager_get(TEST_NAMESPACE, "u32_test", CONFIG_TYPE_UINT32, 
                            &read_val, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test string operations
 */
void test_config_manager_string(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    const char *write_str = TEST_STRING_VALUE;
    char read_str[256] = {0};
    size_t read_size = sizeof(read_str);
    
    // Test set
    esp_err_t ret = config_manager_set(TEST_NAMESPACE, "str_test", CONFIG_TYPE_STRING,
                                      write_str, strlen(write_str) + 1);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test get
    ret = config_manager_get(TEST_NAMESPACE, "str_test", CONFIG_TYPE_STRING,
                            read_str, &read_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(write_str, read_str);
}

/**
 * @brief Test key existence
 */
void test_config_manager_exists(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t test_val = 42;
    
    // Key should not exist initially
    TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, "exists_test"));
    
    // Set the key
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "exists_test", 
                                                CONFIG_TYPE_UINT32, &test_val, sizeof(test_val)));
    
    // Key should exist now
    TEST_ASSERT_TRUE(config_manager_exists(TEST_NAMESPACE, "exists_test"));
}

/**
 * @brief Test key deletion
 */
void test_config_manager_delete(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t test_val = 42;
    
    // Set a key
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "delete_test", 
                                                CONFIG_TYPE_UINT32, &test_val, sizeof(test_val)));
    TEST_ASSERT_TRUE(config_manager_exists(TEST_NAMESPACE, "delete_test"));
    
    // Delete the key
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_delete(TEST_NAMESPACE, "delete_test"));
    TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, "delete_test"));
}

/**
 * @brief Test error handling
 */
void test_config_manager_error_handling(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t test_val = 42;
    
    // Test NULL namespace
    esp_err_t ret = config_manager_set(NULL, "test", CONFIG_TYPE_UINT32, &test_val, sizeof(test_val));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test NULL key
    ret = config_manager_set(TEST_NAMESPACE, NULL, CONFIG_TYPE_UINT32, &test_val, sizeof(test_val));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test key not found
    uint32_t read_val = 0;
    ret = config_manager_get(TEST_NAMESPACE, "nonexistent", CONFIG_TYPE_UINT32, &read_val, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

/**
 * @brief Main test runner function
 */
void app_main(void)
{
    printf("\n=== Config Manager Simple Unit Tests ===\n");
    
    UNITY_BEGIN();
    
    // Basic tests
    RUN_TEST(test_config_manager_init);
    RUN_TEST(test_config_manager_uint32);
    RUN_TEST(test_config_manager_string);
    RUN_TEST(test_config_manager_exists);
    RUN_TEST(test_config_manager_delete);
    RUN_TEST(test_config_manager_error_handling);
    
    UNITY_END();
    
    printf("\n=== Simple Config Manager Tests Completed ===\n");
    
    // Clean up NVS
    nvs_flash_erase();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}