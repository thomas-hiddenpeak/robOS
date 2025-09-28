/**
 * @file test_hardware_hal.c
 * @brief Unit tests for Hardware HAL component
 * 
 * @author robOS Team
 * @date 2025-09-28
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "hardware_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TEST_HARDWARE_HAL";

/**
 * @brief Setup function called before each test
 */
void setUp(void)
{
    // Ensure hardware HAL is deinitialized before each test
    if (hardware_hal_is_initialized()) {
        hardware_hal_deinit();
    }
}

/**
 * @brief Teardown function called after each test
 */
void tearDown(void)
{
    // Clean up after each test
    if (hardware_hal_is_initialized()) {
        hardware_hal_deinit();
    }
}

/**
 * @brief Test hardware HAL initialization and deinitialization
 */
void test_hardware_hal_init_deinit(void)
{
    ESP_LOGI(TAG, "Testing hardware HAL initialization and deinitialization");
    
    // Should not be initialized initially
    TEST_ASSERT_FALSE(hardware_hal_is_initialized());
    
    // Initialize
    esp_err_t ret = hardware_hal_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(hardware_hal_is_initialized());
    
    // Get status
    hal_status_t status;
    ret = hardware_hal_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(status.initialized);
    TEST_ASSERT_EQUAL(0, status.gpio_count);
    TEST_ASSERT_EQUAL(0, status.uart_count);
    TEST_ASSERT_EQUAL(0, status.spi_count);
    TEST_ASSERT_EQUAL(0, status.pwm_count);
    TEST_ASSERT_EQUAL(0, status.adc_count);
    
    // Double initialization should return OK
    ret = hardware_hal_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Deinitialize
    ret = hardware_hal_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(hardware_hal_is_initialized());
    
    // Double deinitialization should return error
    ret = hardware_hal_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * @brief Test GPIO configuration and operations
 */
void test_hardware_hal_gpio(void)
{
    ESP_LOGI(TAG, "Testing hardware HAL GPIO functions");
    
    // Initialize HAL
    esp_err_t ret = hardware_hal_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Configure GPIO as input-output (needed to read back output state)
    hal_gpio_config_t gpio_cfg = {
        .pin = GPIO_NUM_2,  // Built-in LED on many ESP32 boards
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull = GPIO_FLOATING,
        .intr_type = GPIO_INTR_DISABLE,
        .invert = false
    };
    
    ret = hal_gpio_configure(&gpio_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check status
    hal_status_t status;
    ret = hardware_hal_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, status.gpio_count);
    
    // Test GPIO operations
    ret = hal_gpio_set_level(GPIO_NUM_2, 1);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    uint32_t level;
    ret = hal_gpio_get_level(GPIO_NUM_2, &level);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, level);
    
    ret = hal_gpio_set_level(GPIO_NUM_2, 0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = hal_gpio_get_level(GPIO_NUM_2, &level);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, level);
    
    // Test toggle
    ret = hal_gpio_toggle(GPIO_NUM_2);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = hal_gpio_get_level(GPIO_NUM_2, &level);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, level);
    
    // Test invalid arguments
    ret = hal_gpio_set_level(GPIO_NUM_MAX, 1);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = hal_gpio_get_level(GPIO_NUM_MAX, &level);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = hal_gpio_get_level(GPIO_NUM_2, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * @brief Test PWM configuration and operations
 */
void test_hardware_hal_pwm(void)
{
    ESP_LOGI(TAG, "Testing hardware HAL PWM functions");
    
    // Initialize HAL
    esp_err_t ret = hardware_hal_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Configure PWM
    hal_pwm_config_t pwm_cfg = {
        .channel = LEDC_CHANNEL_0,
        .pin = GPIO_NUM_18,
        .timer = LEDC_TIMER_0,
        .frequency = 1000,
        .resolution = LEDC_TIMER_13_BIT,
        .duty_cycle = 4096,  // 50% duty cycle for 13-bit resolution
        .invert = false
    };
    
    ret = hal_pwm_configure(&pwm_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check status
    hal_status_t status;
    ret = hardware_hal_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, status.pwm_count);
    
    // Test PWM operations
    ret = hal_pwm_set_duty(LEDC_CHANNEL_0, 2048);  // 25% duty cycle
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = hal_pwm_set_frequency(LEDC_TIMER_0, 2000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = hal_pwm_start(LEDC_CHANNEL_0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = hal_pwm_stop(LEDC_CHANNEL_0);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test invalid arguments
    ret = hal_pwm_set_duty(HAL_PWM_MAX_CHANNEL, 1000);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = hal_pwm_stop(HAL_PWM_MAX_CHANNEL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * @brief Test ADC configuration and operations
 */
void test_hardware_hal_adc(void)
{
    ESP_LOGI(TAG, "Testing hardware HAL ADC functions");
    
    // Initialize HAL
    esp_err_t ret = hardware_hal_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Configure ADC
    hal_adc_config_t adc_cfg = {
        .unit = ADC_UNIT_1,
        .channel = ADC_CHANNEL_6,  // GPIO34 on ESP32S3
        .attenuation = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    
    ret = hal_adc_configure(&adc_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check status
    hal_status_t status;
    ret = hardware_hal_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, status.adc_count);
    
    // Test ADC reading
    int raw_value;
    ret = hal_adc_read_raw(ADC_UNIT_1, ADC_CHANNEL_6, &raw_value);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(raw_value >= 0 && raw_value <= 4095);  // 12-bit ADC
    
    // Test voltage reading (may not be supported without calibration)
    int voltage_mv;
    ret = hal_adc_read_voltage(ADC_UNIT_1, ADC_CHANNEL_6, &voltage_mv);
    // This may return ESP_ERR_NOT_SUPPORTED if calibration is not available
    if (ret == ESP_OK) {
        TEST_ASSERT_TRUE(voltage_mv >= 0 && voltage_mv <= 3300);  // 0-3.3V range
    }
    
    // Test invalid arguments
    ret = hal_adc_read_raw(ADC_UNIT_1, ADC_CHANNEL_6, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = hal_adc_read_voltage(ADC_UNIT_1, ADC_CHANNEL_6, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * @brief Test error conditions
 */
void test_hardware_hal_error_conditions(void)
{
    ESP_LOGI(TAG, "Testing hardware HAL error conditions");
    
    // Test operations without initialization
    hal_status_t status;
    esp_err_t ret = hardware_hal_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    hal_gpio_config_t gpio_cfg = {
        .pin = GPIO_NUM_2,
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_FLOATING,
        .intr_type = GPIO_INTR_DISABLE,
        .invert = false
    };
    ret = hal_gpio_configure(&gpio_cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
    // Test invalid arguments
    ret = hardware_hal_get_status(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = hal_gpio_configure(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Configure with invalid pin
    gpio_cfg.pin = HAL_GPIO_MAX_PIN;
    ret = hal_gpio_configure(&gpio_cfg);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/**
 * @brief Main test application
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Hardware HAL Unit Tests");
    
    UNITY_BEGIN();
    
    // Basic tests
    RUN_TEST(test_hardware_hal_init_deinit);
    RUN_TEST(test_hardware_hal_gpio);
    RUN_TEST(test_hardware_hal_pwm);
    RUN_TEST(test_hardware_hal_adc);
    RUN_TEST(test_hardware_hal_error_conditions);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "Hardware HAL Unit Tests Completed");
}