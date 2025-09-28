#include "unity.h"
#include "fan_controller.h"
#include "config_manager.h"
#include "hardware_hal.h"
#include "esp_log.h"

#define TEST_FAN_ID 0
#define TEST_GPIO_NUM 4
#define TEST_PWM_CH 0

void setUp(void) {
    hardware_hal_init();
    config_manager_init(NULL);
    fan_controller_init(NULL);
}

void tearDown(void) {
    fan_controller_deinit();
    config_manager_deinit();
    hardware_hal_deinit();
}

void test_fan_controller_init_deinit(void) {
    TEST_ASSERT_TRUE(fan_controller_init(NULL) == ESP_OK);
    TEST_ASSERT_TRUE(fan_controller_deinit() == ESP_OK);
}

void test_fan_controller_is_initialized(void) {
    TEST_ASSERT_TRUE(fan_controller_is_initialized());
}

void test_fan_controller_set_get_speed(void) {
    uint8_t speed;
    
    // Test valid speeds
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_speed(TEST_FAN_ID, 0));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_speed(TEST_FAN_ID, &speed));
    TEST_ASSERT_EQUAL_UINT8(0, speed);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_speed(TEST_FAN_ID, 50));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_speed(TEST_FAN_ID, &speed));
    TEST_ASSERT_EQUAL_UINT8(50, speed);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_speed(TEST_FAN_ID, 100));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_speed(TEST_FAN_ID, &speed));
    TEST_ASSERT_EQUAL_UINT8(100, speed);
}

void test_fan_controller_set_get_mode(void) {
    fan_mode_t mode;
    
    // Test all modes
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_MANUAL));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_mode(TEST_FAN_ID, &mode));
    TEST_ASSERT_EQUAL(FAN_MODE_MANUAL, mode);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_AUTO_TEMP));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_mode(TEST_FAN_ID, &mode));
    TEST_ASSERT_EQUAL(FAN_MODE_AUTO_TEMP, mode);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_AUTO_CURVE));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_mode(TEST_FAN_ID, &mode));
    TEST_ASSERT_EQUAL(FAN_MODE_AUTO_CURVE, mode);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_OFF));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_mode(TEST_FAN_ID, &mode));
    TEST_ASSERT_EQUAL(FAN_MODE_OFF, mode);
}

void test_fan_controller_enable_disable(void) {
    bool enabled;
    
    // Test enable
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_enable(TEST_FAN_ID, true));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_is_enabled(TEST_FAN_ID, &enabled));
    TEST_ASSERT_TRUE(enabled);
    
    // Test disable
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_enable(TEST_FAN_ID, false));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_is_enabled(TEST_FAN_ID, &enabled));
    TEST_ASSERT_FALSE(enabled);
}

void test_fan_controller_get_status(void) {
    fan_status_t status;
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_status(TEST_FAN_ID, &status));
    TEST_ASSERT_EQUAL(TEST_FAN_ID, status.fan_id);
    TEST_ASSERT_TRUE(status.enabled);  // Should be enabled by default
    TEST_ASSERT_EQUAL(FAN_MODE_MANUAL, status.mode);  // Should be manual by default
    TEST_ASSERT_EQUAL_UINT8(0, status.speed_percent);  // Should be 0 by default
}

void test_fan_controller_get_all_status(void) {
    fan_status_t status_array[4];
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_all_status(status_array, 4));
    TEST_ASSERT_EQUAL(0, status_array[0].fan_id);
    TEST_ASSERT_EQUAL(1, status_array[1].fan_id);
    TEST_ASSERT_EQUAL(2, status_array[2].fan_id);
    TEST_ASSERT_EQUAL(3, status_array[3].fan_id);
}

void test_fan_controller_update_temperature(void) {
    // Set to auto temp mode first
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_AUTO_TEMP));
    
    // Update temperature
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_update_temperature(TEST_FAN_ID, 25.0f));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_update_temperature(TEST_FAN_ID, 50.0f));
}

void test_fan_controller_set_curve(void) {
    fan_curve_point_t curve_points[3] = {
        {20.0f, 0},   // 20°C -> 0%
        {40.0f, 50},  // 40°C -> 50%
        {60.0f, 100}  // 60°C -> 100%
    };
    
    // Set to auto curve mode first
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_AUTO_CURVE));
    
    // Set curve
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_curve(TEST_FAN_ID, curve_points, 3));
    
    // Update temperature to test curve
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_update_temperature(TEST_FAN_ID, 30.0f));
}

void test_fan_controller_configure_gpio(void) {
    // Configure GPIO
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_configure_gpio(TEST_FAN_ID, 5, 1));
    
    // Verify by checking status (GPIO config should be updated)
    fan_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_status(TEST_FAN_ID, &status));
    // Note: We can't directly verify GPIO config without additional getter functions
}

void test_fan_controller_save_load_config(void) {
    // Set some parameters
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_speed(TEST_FAN_ID, 75));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_AUTO_TEMP));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_enable(TEST_FAN_ID, false));
    
    // Save configuration
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_save_config(TEST_FAN_ID));
    
    // Change parameters
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_speed(TEST_FAN_ID, 25));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_mode(TEST_FAN_ID, FAN_MODE_MANUAL));
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_enable(TEST_FAN_ID, true));
    
    // Load configuration
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_load_config(TEST_FAN_ID));
    
    // Verify parameters were restored
    uint8_t speed;
    fan_mode_t mode;
    bool enabled;
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_speed(TEST_FAN_ID, &speed));
    TEST_ASSERT_EQUAL_UINT8(75, speed);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_get_mode(TEST_FAN_ID, &mode));
    TEST_ASSERT_EQUAL(FAN_MODE_AUTO_TEMP, mode);
    
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_is_enabled(TEST_FAN_ID, &enabled));
    TEST_ASSERT_FALSE(enabled);
}

void test_fan_controller_invalid_parameters(void) {
    uint8_t speed;
    fan_mode_t mode;
    bool enabled;
    fan_status_t status;
    
    // Test invalid fan ID
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_set_speed(99, 50));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_speed(99, &speed));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_set_mode(99, FAN_MODE_MANUAL));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_mode(99, &mode));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_enable(99, true));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_is_enabled(99, &enabled));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_status(99, &status));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_update_temperature(99, 25.0f));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_configure_gpio(99, 5, 1));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_save_config(99));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_load_config(99));
    
    // Test invalid speed values (should be clamped or rejected)
    TEST_ASSERT_EQUAL(ESP_OK, fan_controller_set_speed(TEST_FAN_ID, 150)); // Should be OK (clamped to 100)
    
    // Test NULL pointers
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_speed(TEST_FAN_ID, NULL));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_mode(TEST_FAN_ID, NULL));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_is_enabled(TEST_FAN_ID, NULL));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_status(TEST_FAN_ID, NULL));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, fan_controller_get_all_status(NULL, 4));
}

void test_fan_controller_get_default_config(void) {
    fan_controller_config_t config = fan_controller_get_default_config();
    
    TEST_ASSERT_TRUE(config.num_fans > 0);
    TEST_ASSERT_NOT_NULL(config.fan_configs);
    TEST_ASSERT_TRUE(config.update_interval_ms > 0);
}

int app_main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_fan_controller_init_deinit);
    RUN_TEST(test_fan_controller_is_initialized);
    RUN_TEST(test_fan_controller_set_get_speed);
    RUN_TEST(test_fan_controller_set_get_mode);
    RUN_TEST(test_fan_controller_enable_disable);
    RUN_TEST(test_fan_controller_get_status);
    RUN_TEST(test_fan_controller_get_all_status);
    RUN_TEST(test_fan_controller_update_temperature);
    RUN_TEST(test_fan_controller_set_curve);
    RUN_TEST(test_fan_controller_configure_gpio);
    RUN_TEST(test_fan_controller_save_load_config);
    RUN_TEST(test_fan_controller_invalid_parameters);
    RUN_TEST(test_fan_controller_get_default_config);
    
    return UNITY_END();
}
