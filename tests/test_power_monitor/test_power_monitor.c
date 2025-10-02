/**
 * @file test_power_monitor.c
 * @brief Unit tests for Power Monitor Component
 * 
 * This file contains comprehensive unit tests for the power monitor component,
 * including voltage monitoring, power chip communication, and configuration management.
 * 
 * @author robOS Team
 * @date 2025-10-02
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_monitor.h"

static const char *TAG = "power_monitor_test";

// Test configuration
static power_monitor_config_t test_config;
static bool callback_triggered = false;
static power_monitor_event_type_t last_event_type;
static void *last_event_data = NULL;

/**
 * @brief Test event callback function
 */
static void test_event_callback(power_monitor_event_type_t event_type, void *event_data, void *user_data)
{
    ESP_LOGI(TAG, "Event callback triggered: type=%d", event_type);
    callback_triggered = true;
    last_event_type = event_type;
    last_event_data = event_data;
}

/**
 * @brief Setup function called before each test
 */
void setUp(void)
{
    ESP_LOGI(TAG, "Setting up test environment");
    
    // Reset callback state
    callback_triggered = false;
    last_event_type = POWER_MONITOR_EVENT_MAX;
    last_event_data = NULL;
    
    // Get default configuration
    esp_err_t ret = power_monitor_get_default_config(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Modify config for testing (shorter intervals for faster tests)
    test_config.voltage_config.sample_interval_ms = 100;  // 100ms for faster testing
    test_config.auto_start_monitoring = false;            // Manual start for controlled testing
}

/**
 * @brief Teardown function called after each test
 */
void tearDown(void)
{
    ESP_LOGI(TAG, "Tearing down test environment");
    
    // Stop and deinitialize power monitor
    power_monitor_stop();
    power_monitor_deinit();
    
    // Small delay to ensure cleanup
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief Test 1: Basic initialization and deinitialization
 */
void test_power_monitor_init_deinit(void)
{
    ESP_LOGI(TAG, "Testing power monitor initialization and deinitialization");
    
    // Test initialization with valid config
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify component is initialized but not running
    TEST_ASSERT_FALSE(power_monitor_is_running());
    
    // Test deinitialization
    ret = power_monitor_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test double initialization (should fail)
    ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * @brief Test 2: Power monitor start and stop
 */
void test_power_monitor_start_stop(void)
{
    ESP_LOGI(TAG, "Testing power monitor start and stop");
    
    // Initialize first
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test start
    ret = power_monitor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(power_monitor_is_running());
    
    // Test double start (should succeed with warning)
    ret = power_monitor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test stop
    ret = power_monitor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(power_monitor_is_running());
    
    // Test double stop (should succeed)
    ret = power_monitor_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Test 3: Default configuration validation
 */
void test_power_monitor_default_config(void)
{
    ESP_LOGI(TAG, "Testing default configuration validation");
    
    power_monitor_config_t config;
    esp_err_t ret = power_monitor_get_default_config(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Validate voltage monitoring config
    TEST_ASSERT_EQUAL(18, config.voltage_config.gpio_pin);
    TEST_ASSERT_EQUAL_FLOAT(11.4f, config.voltage_config.divider_ratio);
    TEST_ASSERT_EQUAL(1000, config.voltage_config.sample_interval_ms);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, config.voltage_config.voltage_min_threshold);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, config.voltage_config.voltage_max_threshold);
    TEST_ASSERT_TRUE(config.voltage_config.enable_threshold_alarm);
    
    // Validate power chip config
    TEST_ASSERT_EQUAL(UART_NUM_1, config.power_chip_config.uart_num);
    TEST_ASSERT_EQUAL(47, config.power_chip_config.rx_gpio_pin);
    TEST_ASSERT_EQUAL(9600, config.power_chip_config.baud_rate);
    TEST_ASSERT_EQUAL(1000, config.power_chip_config.timeout_ms);
    TEST_ASSERT_FALSE(config.power_chip_config.enable_protocol_debug);
    
    // Validate task config
    TEST_ASSERT_TRUE(config.auto_start_monitoring);
    TEST_ASSERT_EQUAL(4096, config.task_stack_size);
    TEST_ASSERT_EQUAL(5, config.task_priority);
}

/**
 * @brief Test 4: Voltage threshold management
 */
void test_power_monitor_voltage_thresholds(void)
{
    ESP_LOGI(TAG, "Testing voltage threshold management");
    
    // Initialize component
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test setting valid thresholds
    ret = power_monitor_set_voltage_thresholds(12.0f, 24.0f);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test getting thresholds
    float min_thresh, max_thresh;
    ret = power_monitor_get_voltage_thresholds(&min_thresh, &max_thresh);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, min_thresh);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, max_thresh);
    
    // Test invalid thresholds (min >= max)
    ret = power_monitor_set_voltage_thresholds(24.0f, 12.0f);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = power_monitor_set_voltage_thresholds(15.0f, 15.0f);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test negative thresholds
    ret = power_monitor_set_voltage_thresholds(-5.0f, 24.0f);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test threshold alarm enable/disable
    ret = power_monitor_set_threshold_alarm(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = power_monitor_set_threshold_alarm(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Test 5: Sample interval configuration
 */
void test_power_monitor_sample_interval(void)
{
    ESP_LOGI(TAG, "Testing sample interval configuration");
    
    // Initialize component
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test setting valid interval
    ret = power_monitor_set_sample_interval(500);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test getting interval
    uint32_t interval;
    ret = power_monitor_get_sample_interval(&interval);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(500, interval);
    
    // Test invalid intervals (too small)
    ret = power_monitor_set_sample_interval(50);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test invalid intervals (too large)
    ret = power_monitor_set_sample_interval(70000);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test boundary values
    ret = power_monitor_set_sample_interval(100);  // Minimum
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = power_monitor_set_sample_interval(60000); // Maximum
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Test 6: Event callback registration
 */
void test_power_monitor_event_callback(void)
{
    ESP_LOGI(TAG, "Testing event callback registration");
    
    // Initialize component
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test callback registration
    ret = power_monitor_register_callback(test_event_callback, (void*)0x12345678);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test callback unregistration
    ret = power_monitor_unregister_callback();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test multiple registrations (should replace previous)
    ret = power_monitor_register_callback(test_event_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = power_monitor_register_callback(test_event_callback, (void*)0xABCDEF);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Test 7: Debug mode configuration
 */
void test_power_monitor_debug_mode(void)
{
    ESP_LOGI(TAG, "Testing debug mode configuration");
    
    // Initialize component
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test enabling debug mode
    ret = power_monitor_set_debug_mode(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test disabling debug mode
    ret = power_monitor_set_debug_mode(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Test 8: Data retrieval functions
 */
void test_power_monitor_data_retrieval(void)
{
    ESP_LOGI(TAG, "Testing data retrieval functions");
    
    // Initialize and start component
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = power_monitor_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait a bit for some data to be collected
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Test voltage data retrieval
    voltage_data_t voltage_data;
    ret = power_monitor_get_voltage_data(&voltage_data);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Basic validation (voltage should be reasonable for testing)
    TEST_ASSERT_GREATER_OR_EQUAL(0.0f, voltage_data.voltage_v);
    TEST_ASSERT_LESS_THAN(50.0f, voltage_data.voltage_v); // Reasonable upper bound
    TEST_ASSERT_GREATER_THAN(0, voltage_data.timestamp_us);
    
    // Test power chip data retrieval (may not have valid data in test environment)
    power_chip_data_t power_data;
    ret = power_monitor_get_power_chip_data(&power_data);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test statistics retrieval
    power_monitor_stats_t stats;
    ret = power_monitor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Validate statistics
    TEST_ASSERT_GREATER_THAN(0, stats.uptime_ms);
    TEST_ASSERT_GREATER_OR_EQUAL(0, stats.voltage_samples);
    TEST_ASSERT_GREATER_OR_EQUAL(0, stats.power_chip_packets);
    TEST_ASSERT_GREATER_OR_EQUAL(0, stats.crc_errors);
    
    // Test statistics reset
    ret = power_monitor_reset_stats();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify stats are reset
    ret = power_monitor_get_stats(&stats);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, stats.voltage_samples);
    TEST_ASSERT_EQUAL(0, stats.power_chip_packets);
    TEST_ASSERT_EQUAL(0, stats.crc_errors);
}

/**
 * @brief Test 9: Invalid parameter handling
 */
void test_power_monitor_invalid_parameters(void)
{
    ESP_LOGI(TAG, "Testing invalid parameter handling");
    
    // Test with NULL config
    esp_err_t ret = power_monitor_init(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test with NULL pointers for data retrieval (before init)
    voltage_data_t voltage_data;
    ret = power_monitor_get_voltage_data(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = power_monitor_get_voltage_data(&voltage_data);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret); // Not initialized
    
    power_chip_data_t power_data;
    ret = power_monitor_get_power_chip_data(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    power_monitor_stats_t stats;
    ret = power_monitor_get_stats(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test threshold functions with NULL pointers
    float min_thresh, max_thresh;
    ret = power_monitor_get_voltage_thresholds(NULL, &max_thresh);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = power_monitor_get_voltage_thresholds(&min_thresh, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test sample interval with NULL pointer
    ret = power_monitor_get_sample_interval(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * @brief Test 10: Configuration persistence (placeholder)
 */
void test_power_monitor_config_persistence(void)
{
    ESP_LOGI(TAG, "Testing configuration persistence (placeholder)");
    
    // Initialize component
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test configuration save (not yet implemented)
    ret = power_monitor_save_config();
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, ret);
    
    // Test configuration load (not yet implemented)
    ret = power_monitor_load_config();
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, ret);
    
    ESP_LOGI(TAG, "Configuration persistence tests completed (features not yet implemented)");
}

/**
 * @brief Test 11: Auto-start functionality
 */
void test_power_monitor_auto_start(void)
{
    ESP_LOGI(TAG, "Testing auto-start functionality");
    
    // Modify config to enable auto-start
    test_config.auto_start_monitoring = true;
    
    // Initialize with auto-start enabled
    esp_err_t ret = power_monitor_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Small delay to allow auto-start
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Verify component is running
    TEST_ASSERT_TRUE(power_monitor_is_running());
}

/**
 * @brief Test 12: Component state validation
 */
void test_power_monitor_state_validation(void)
{
    ESP_LOGI(TAG, "Testing component state validation");
    
    // Test operations before initialization
    esp_err_t ret = power_monitor_start();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_stop();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_set_voltage_thresholds(10.0f, 20.0f);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_set_threshold_alarm(true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_set_sample_interval(500);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_register_callback(test_event_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_unregister_callback();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_set_debug_mode(true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    ret = power_monitor_reset_stats();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    // Verify component is not running
    TEST_ASSERT_FALSE(power_monitor_is_running());
}

/**
 * @brief Run all power monitor tests
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Power Monitor Component Tests");
    ESP_LOGI(TAG, "=====================================");
    
    UNITY_BEGIN();
    
    // Basic functionality tests
    RUN_TEST(test_power_monitor_init_deinit);
    RUN_TEST(test_power_monitor_start_stop);
    RUN_TEST(test_power_monitor_default_config);
    
    // Configuration tests  
    RUN_TEST(test_power_monitor_voltage_thresholds);
    RUN_TEST(test_power_monitor_sample_interval);
    RUN_TEST(test_power_monitor_event_callback);
    RUN_TEST(test_power_monitor_debug_mode);
    
    // Data and operation tests
    RUN_TEST(test_power_monitor_data_retrieval);
    RUN_TEST(test_power_monitor_invalid_parameters);
    RUN_TEST(test_power_monitor_config_persistence);
    RUN_TEST(test_power_monitor_auto_start);
    RUN_TEST(test_power_monitor_state_validation);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "All Power Monitor Component Tests Completed");
}