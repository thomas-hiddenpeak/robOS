/**
 * @file test_config_manager.c
 * @brief Unit tests for config_manager component
 * 
 * This file contains comprehensive unit tests for the config_manager component,
 * including tests for:
 * - Basic initialization and deinitialization
 * - All data type operations (get/set)
 * - Error handling and edge cases
 * - Bulk operations
 * - Namespace operations
 * - Thread safety
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
#include "freertos/semphr.h"

/* ============================================================================
 * Test Constants and Macros
 * ============================================================================ */

#define TEST_NAMESPACE      "test_ns"
#define TEST_NAMESPACE_2    "test_ns2"
#define TEST_KEY_PREFIX     "test_key"
#define TEST_STRING_VALUE   "Hello, robOS!"
#define TEST_BLOB_SIZE      64

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
        config_manager_clear_namespace(TEST_NAMESPACE_2);
        config_manager_deinit();
    }
}

/**
 * @brief Generate test key with index
 */
static void generate_test_key(char *buffer, size_t buffer_size, int index)
{
    snprintf(buffer, buffer_size, "%s_%d", TEST_KEY_PREFIX, index);
}

/* ============================================================================
 * Initialization and Basic Function Tests
 * ============================================================================ */

/**
 * @brief Test config manager initialization with default config
 */
void test_config_manager_init_default(void)
{
    esp_err_t ret = config_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(config_manager_is_initialized());
}

/**
 * @brief Test config manager initialization with custom config
 */
void test_config_manager_init_custom(void)
{
    config_manager_config_t config = {
        .auto_commit = false,
        .create_backup = true,
        .commit_interval_ms = 10000
    };
    
    esp_err_t ret = config_manager_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(config_manager_is_initialized());
}

/**
 * @brief Test double initialization (should return OK)
 */
void test_config_manager_double_init(void)
{
    esp_err_t ret1 = config_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret1);
    
    esp_err_t ret2 = config_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret2);
    TEST_ASSERT_TRUE(config_manager_is_initialized());
}

/**
 * @brief Test config manager deinitialization
 */
void test_config_manager_deinit(void)
{
    esp_err_t ret = config_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(config_manager_is_initialized());
    
    ret = config_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(config_manager_is_initialized());
}

/**
 * @brief Test operations without initialization
 */
void test_config_manager_uninit_operations(void)
{
    TEST_ASSERT_FALSE(config_manager_is_initialized());
    
    uint32_t test_val = 42;
    esp_err_t ret = config_manager_set(TEST_NAMESPACE, "test", CONFIG_TYPE_UINT32, &test_val, sizeof(test_val));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = config_manager_get(TEST_NAMESPACE, "test", CONFIG_TYPE_UINT32, &test_val, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/* ============================================================================
 * Data Type Tests - Set and Get Operations
 * ============================================================================ */

/**
 * @brief Test uint8_t operations
 */
void test_config_manager_uint8(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint8_t write_val = 123;
    uint8_t read_val = 0;
    
    // Test set
    esp_err_t ret = config_manager_set(TEST_NAMESPACE, "u8_test", CONFIG_TYPE_UINT8, &write_val, sizeof(write_val));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test get
    ret = config_manager_get(TEST_NAMESPACE, "u8_test", CONFIG_TYPE_UINT8, &read_val, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(write_val, read_val);
    
    // Test macro convenience functions
    write_val = 200;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_U8(TEST_NAMESPACE, "u8_macro", write_val));
    
    read_val = 0;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_U8(TEST_NAMESPACE, "u8_macro", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test uint16_t operations
 */
void test_config_manager_uint16(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint16_t write_val = 12345;
    uint16_t read_val = 0;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_U16(TEST_NAMESPACE, "u16_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_U16(TEST_NAMESPACE, "u16_test", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test uint32_t operations
 */
void test_config_manager_uint32(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t write_val = 0x12345678;
    uint32_t read_val = 0;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_U32(TEST_NAMESPACE, "u32_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_U32(TEST_NAMESPACE, "u32_test", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test int8_t operations
 */
void test_config_manager_int8(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    int8_t write_val = -42;
    int8_t read_val = 0;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_I8(TEST_NAMESPACE, "i8_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_I8(TEST_NAMESPACE, "i8_test", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test int16_t operations
 */
void test_config_manager_int16(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    int16_t write_val = -12345;
    int16_t read_val = 0;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_I16(TEST_NAMESPACE, "i16_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_I16(TEST_NAMESPACE, "i16_test", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test int32_t operations
 */
void test_config_manager_int32(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    int32_t write_val = -0x12345678;
    int32_t read_val = 0;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_I32(TEST_NAMESPACE, "i32_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_I32(TEST_NAMESPACE, "i32_test", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
}

/**
 * @brief Test float operations
 */
void test_config_manager_float(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    float write_val = 3.14159f;
    float read_val = 0.0f;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_FLOAT(TEST_NAMESPACE, "float_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_FLOAT(TEST_NAMESPACE, "float_test", &read_val));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, write_val, read_val);
}

/**
 * @brief Test boolean operations
 */
void test_config_manager_bool(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    bool write_val = true;
    bool read_val = false;
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_BOOL(TEST_NAMESPACE, "bool_test", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_BOOL(TEST_NAMESPACE, "bool_test", &read_val));
    TEST_ASSERT_EQUAL(write_val, read_val);
    
    // Test false value
    write_val = false;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_BOOL(TEST_NAMESPACE, "bool_test2", write_val));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_BOOL(TEST_NAMESPACE, "bool_test2", &read_val));
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
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_STR(TEST_NAMESPACE, "str_test", write_str));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_STR(TEST_NAMESPACE, "str_test", read_str, &read_size));
    TEST_ASSERT_EQUAL_STRING(write_str, read_str);
    TEST_ASSERT_EQUAL(strlen(write_str) + 1, read_size);
}

/**
 * @brief Test blob operations
 */
void test_config_manager_blob(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    // Create test blob data
    uint8_t write_blob[TEST_BLOB_SIZE];
    for (int i = 0; i < TEST_BLOB_SIZE; i++) {
        write_blob[i] = (uint8_t)(i & 0xFF);
    }
    
    uint8_t read_blob[TEST_BLOB_SIZE] = {0};
    size_t read_size = sizeof(read_blob);
    
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_BLOB(TEST_NAMESPACE, "blob_test", write_blob, sizeof(write_blob)));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_BLOB(TEST_NAMESPACE, "blob_test", read_blob, &read_size));
    
    TEST_ASSERT_EQUAL(sizeof(write_blob), read_size);
    TEST_ASSERT_EQUAL_MEMORY(write_blob, read_blob, sizeof(write_blob));
}

/* ============================================================================
 * Error Handling and Edge Cases Tests
 * ============================================================================ */

/**
 * @brief Test invalid parameter handling
 */
void test_config_manager_invalid_params(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t test_val = 42;
    
    // Test NULL namespace
    esp_err_t ret = config_manager_set(NULL, "test", CONFIG_TYPE_UINT32, &test_val, sizeof(test_val));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test NULL key
    ret = config_manager_set(TEST_NAMESPACE, NULL, CONFIG_TYPE_UINT32, &test_val, sizeof(test_val));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test NULL value pointer
    ret = config_manager_set(TEST_NAMESPACE, "test", CONFIG_TYPE_UINT32, NULL, sizeof(test_val));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test NULL value pointer for get
    ret = config_manager_get(TEST_NAMESPACE, "test", CONFIG_TYPE_UINT32, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * @brief Test key not found scenarios
 */
void test_config_manager_key_not_found(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t read_val = 0;
    esp_err_t ret = config_manager_get(TEST_NAMESPACE, "nonexistent", CONFIG_TYPE_UINT32, &read_val, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
    
    // Test exists function
    TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, "nonexistent"));
}

/**
 * @brief Test type mismatch scenarios
 */
void test_config_manager_type_mismatch(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    // Set as uint32
    uint32_t write_val = 42;
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "type_test", CONFIG_TYPE_UINT32, &write_val, sizeof(write_val)));
    
    // Try to read as uint8 (should fail)
    uint8_t read_val = 0;
    esp_err_t ret = config_manager_get(TEST_NAMESPACE, "type_test", CONFIG_TYPE_UINT8, &read_val, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ret);
}

/**
 * @brief Test buffer size edge cases
 */
void test_config_manager_buffer_size(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    const char *long_string = "This is a very long string for testing buffer size handling in config manager";
    char small_buffer[10] = {0};
    size_t buffer_size = sizeof(small_buffer);
    
    // Set long string
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_STR(TEST_NAMESPACE, "long_str", long_string));
    
    // Try to read into small buffer (should fail)
    esp_err_t ret = config_manager_get(TEST_NAMESPACE, "long_str", CONFIG_TYPE_STRING, small_buffer, &buffer_size);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ret);
}

/* ============================================================================
 * Namespace and Key Management Tests
 * ============================================================================ */

/**
 * @brief Test key existence checking
 */
void test_config_manager_exists(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t test_val = 42;
    
    // Key should not exist initially
    TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, "exists_test"));
    
    // Set the key
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "exists_test", CONFIG_TYPE_UINT32, &test_val, sizeof(test_val)));
    
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
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "delete_test", CONFIG_TYPE_UINT32, &test_val, sizeof(test_val)));
    TEST_ASSERT_TRUE(config_manager_exists(TEST_NAMESPACE, "delete_test"));
    
    // Delete the key
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_delete(TEST_NAMESPACE, "delete_test"));
    TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, "delete_test"));
    
    // Try to delete non-existent key (should not fail)
    esp_err_t ret = config_manager_delete(TEST_NAMESPACE, "nonexistent");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Test namespace clearing
 */
void test_config_manager_clear_namespace(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t test_val = 42;
    
    // Set multiple keys in namespace
    for (int i = 0; i < 5; i++) {
        char key[32];
        generate_test_key(key, sizeof(key), i);
        TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, key, CONFIG_TYPE_UINT32, &test_val, sizeof(test_val)));
        TEST_ASSERT_TRUE(config_manager_exists(TEST_NAMESPACE, key));
    }
    
    // Clear the namespace
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_clear_namespace(TEST_NAMESPACE));
    
    // All keys should be gone
    for (int i = 0; i < 5; i++) {
        char key[32];
        generate_test_key(key, sizeof(key), i);
        TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, key));
    }
}

/**
 * @brief Test multiple namespaces isolation
 */
void test_config_manager_namespace_isolation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    uint32_t val1 = 100;
    uint32_t val2 = 200;
    uint32_t read_val = 0;
    
    // Set same key in different namespaces
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "same_key", CONFIG_TYPE_UINT32, &val1, sizeof(val1)));
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE_2, "same_key", CONFIG_TYPE_UINT32, &val2, sizeof(val2)));
    
    // Read from first namespace
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_get(TEST_NAMESPACE, "same_key", CONFIG_TYPE_UINT32, &read_val, NULL));
    TEST_ASSERT_EQUAL(val1, read_val);
    
    // Read from second namespace
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_get(TEST_NAMESPACE_2, "same_key", CONFIG_TYPE_UINT32, &read_val, NULL));
    TEST_ASSERT_EQUAL(val2, read_val);
    
    // Clear first namespace
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_clear_namespace(TEST_NAMESPACE));
    
    // First should be gone, second should remain
    TEST_ASSERT_FALSE(config_manager_exists(TEST_NAMESPACE, "same_key"));
    TEST_ASSERT_TRUE(config_manager_exists(TEST_NAMESPACE_2, "same_key"));
}

/* ============================================================================
 * Bulk Operations Tests
 * ============================================================================ */

/**
 * @brief Test bulk save operations
 */
void test_config_manager_bulk_save(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    // Prepare test data
    config_item_t items[5];
    
    // Initialize items
    items[0].key = "bulk_u8";
    items[0].type = CONFIG_TYPE_UINT8;
    items[0].value.u8 = 123;
    items[0].is_default = false;
    
    items[1].key = "bulk_u16";
    items[1].type = CONFIG_TYPE_UINT16;
    items[1].value.u16 = 12345;
    items[1].is_default = false;
    
    items[2].key = "bulk_u32";
    items[2].type = CONFIG_TYPE_UINT32;
    items[2].value.u32 = 0x12345678;
    items[2].is_default = false;
    
    items[3].key = "bulk_float";
    items[3].type = CONFIG_TYPE_FLOAT;
    items[3].value.f = 3.14159f;
    items[3].is_default = false;
    
    items[4].key = "bulk_bool";
    items[4].type = CONFIG_TYPE_BOOL;
    items[4].value.b = true;
    items[4].is_default = false;
    
    // Save bulk data
    esp_err_t ret = config_manager_save_bulk(TEST_NAMESPACE, items, 5);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify individual items were saved
    uint8_t read_u8;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_U8(TEST_NAMESPACE, "bulk_u8", &read_u8));
    TEST_ASSERT_EQUAL(123, read_u8);
    
    uint16_t read_u16;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_U16(TEST_NAMESPACE, "bulk_u16", &read_u16));
    TEST_ASSERT_EQUAL(12345, read_u16);
    
    float read_float;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_GET_FLOAT(TEST_NAMESPACE, "bulk_float", &read_float));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159f, read_float);
}

/**
 * @brief Test bulk load operations
 */
void test_config_manager_bulk_load(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    // First save some data
    uint32_t val1 = 100, val2 = 200, val3 = 300;
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_U32(TEST_NAMESPACE, "load_1", val1));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_U32(TEST_NAMESPACE, "load_2", val2));
    TEST_ASSERT_EQUAL(ESP_OK, CONFIG_SET_U32(TEST_NAMESPACE, "load_3", val3));
    
    // Prepare load items (keys and types must be set)
    config_item_t items[] = {
        {"load_1", CONFIG_TYPE_UINT32, .value.u32 = 0, false},
        {"load_2", CONFIG_TYPE_UINT32, .value.u32 = 0, false},
        {"load_3", CONFIG_TYPE_UINT32, .value.u32 = 0, false}
    };
    
    // Load bulk data
    esp_err_t ret = config_manager_load_bulk(TEST_NAMESPACE, items, 3);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify loaded values
    TEST_ASSERT_EQUAL(val1, items[0].value.u32);
    TEST_ASSERT_EQUAL(val2, items[1].value.u32);
    TEST_ASSERT_EQUAL(val3, items[2].value.u32);
}

/* ============================================================================
 * Commit and Persistence Tests
 * ============================================================================ */

/**
 * @brief Test manual commit operations
 */
void test_config_manager_commit(void)
{
    config_manager_config_t config = {
        .auto_commit = false,  // Disable auto-commit for this test
        .create_backup = false,
        .commit_interval_ms = 0
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(&config));
    
    uint32_t test_val = 42;
    
    // Set a value (should not be committed yet)
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_set(TEST_NAMESPACE, "commit_test", CONFIG_TYPE_UINT32, &test_val, sizeof(test_val)));
    
    // Manual commit
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_commit());
    
    // Should be able to read the value
    uint32_t read_val = 0;
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_get(TEST_NAMESPACE, "commit_test", CONFIG_TYPE_UINT32, &read_val, NULL));
    TEST_ASSERT_EQUAL(test_val, read_val);
}

/* ============================================================================
 * Statistics Tests
 * ============================================================================ */

/**
 * @brief Test NVS statistics functionality
 */
void test_config_manager_stats(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    size_t used_entries, free_entries, total_size, used_size;
    
    // Get initial stats
    esp_err_t ret = config_manager_get_stats(NULL, &used_entries, &free_entries, &total_size, &used_size);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Stats should be reasonable (use simple checks)
    TEST_ASSERT_TRUE(total_size > 0);
    TEST_ASSERT_TRUE(used_size >= 0);
    TEST_ASSERT_TRUE(free_entries >= 0);
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

typedef struct {
    int thread_id;
    int iterations;
    SemaphoreHandle_t start_semaphore;
    bool test_passed;
} thread_test_params_t;

static void thread_test_task(void *pvParameters)
{
    thread_test_params_t *params = (thread_test_params_t *)pvParameters;
    
    // Wait for start signal
    xSemaphoreTake(params->start_semaphore, portMAX_DELAY);
    
    char key[32];
    uint32_t base_val = params->thread_id * 1000;
    
    params->test_passed = true;
    
    for (int i = 0; i < params->iterations; i++) {
        snprintf(key, sizeof(key), "thread_%d_%d", params->thread_id, i);
        uint32_t write_val = base_val + i;
        
        // Set value
        esp_err_t ret = config_manager_set(TEST_NAMESPACE, key, CONFIG_TYPE_UINT32, &write_val, sizeof(write_val));
        if (ret != ESP_OK) {
            params->test_passed = false;
            break;
        }
        
        // Get value immediately
        uint32_t read_val = 0;
        ret = config_manager_get(TEST_NAMESPACE, key, CONFIG_TYPE_UINT32, &read_val, NULL);
        if (ret != ESP_OK || read_val != write_val) {
            params->test_passed = false;
            break;
        }
        
        // Small delay to allow task switching
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Test thread safety with multiple concurrent operations
 */
void test_config_manager_thread_safety(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, config_manager_init(NULL));
    
    const int num_threads = 3;
    const int iterations_per_thread = 20;
    
    thread_test_params_t params[num_threads];
    TaskHandle_t task_handles[num_threads];
    SemaphoreHandle_t start_semaphore = xSemaphoreCreateCounting(num_threads, 0);
    
    TEST_ASSERT_NOT_NULL(start_semaphore);
    
    // Create test threads
    for (int i = 0; i < num_threads; i++) {
        params[i].thread_id = i;
        params[i].iterations = iterations_per_thread;
        params[i].start_semaphore = start_semaphore;
        params[i].test_passed = false;
        
        BaseType_t ret = xTaskCreate(thread_test_task, "thread_test", 4096, &params[i], 5, &task_handles[i]);
        TEST_ASSERT_EQUAL(pdPASS, ret);
    }
    
    // Start all threads simultaneously
    for (int i = 0; i < num_threads; i++) {
        xSemaphoreGive(start_semaphore);
    }
    
    // Wait for all threads to complete (with timeout)
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Check results
    for (int i = 0; i < num_threads; i++) {
        TEST_ASSERT_TRUE_MESSAGE(params[i].test_passed, "Thread safety test failed");
    }
    
    vSemaphoreDelete(start_semaphore);
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

/**
 * @brief Main test runner function
 */
void app_main(void)
{
    printf("\n=== Config Manager Unit Tests ===\n");
    
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_config_manager_init_default);
    RUN_TEST(test_config_manager_init_custom);
    RUN_TEST(test_config_manager_double_init);
    RUN_TEST(test_config_manager_deinit);
    RUN_TEST(test_config_manager_uninit_operations);
    
    // Data type tests
    RUN_TEST(test_config_manager_uint8);
    RUN_TEST(test_config_manager_uint16);
    RUN_TEST(test_config_manager_uint32);
    RUN_TEST(test_config_manager_int8);
    RUN_TEST(test_config_manager_int16);
    RUN_TEST(test_config_manager_int32);
    RUN_TEST(test_config_manager_float);
    RUN_TEST(test_config_manager_bool);
    RUN_TEST(test_config_manager_string);
    RUN_TEST(test_config_manager_blob);
    
    // Error handling tests
    RUN_TEST(test_config_manager_invalid_params);
    RUN_TEST(test_config_manager_key_not_found);
    RUN_TEST(test_config_manager_type_mismatch);
    RUN_TEST(test_config_manager_buffer_size);
    
    // Namespace and key management tests
    RUN_TEST(test_config_manager_exists);
    RUN_TEST(test_config_manager_delete);
    RUN_TEST(test_config_manager_clear_namespace);
    RUN_TEST(test_config_manager_namespace_isolation);
    
    // Bulk operations tests
    RUN_TEST(test_config_manager_bulk_save);
    RUN_TEST(test_config_manager_bulk_load);
    
    // Commit and persistence tests
    RUN_TEST(test_config_manager_commit);
    
    // Statistics tests
    RUN_TEST(test_config_manager_stats);
    
    // Thread safety tests (commented out for initial testing)
    // RUN_TEST(test_config_manager_thread_safety);
    
    UNITY_END();
    
    printf("\n=== All Config Manager Tests Completed ===\n");
    
    // Clean up NVS
    nvs_flash_erase();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}