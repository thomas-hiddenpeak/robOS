/**
 * @file hardware_hal.h
 * @brief Hardware Abstraction Layer for robOS
 * 
 * This module provides a unified interface for accessing hardware peripherals
 * on the ESP32S3 platform. It abstracts GPIO, PWM, SPI, ADC, UART and other
 * hardware interfaces to provide a consistent API for higher-level components.
 * 
 * @author robOS Team
 * @date 2025-09-28
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hardware HAL version
 */
#define HARDWARE_HAL_VERSION_MAJOR  1
#define HARDWARE_HAL_VERSION_MINOR  0
#define HARDWARE_HAL_VERSION_PATCH  0

/**
 * @brief Maximum number of GPIO pins
 */
#define HAL_GPIO_MAX_PIN    48

/**
 * @brief Maximum number of UART ports
 */
#define HAL_UART_MAX_PORT   3

/**
 * @brief Maximum number of SPI hosts
 */
#define HAL_SPI_MAX_HOST    3

/**
 * @brief Maximum number of PWM channels
 */
#define HAL_PWM_MAX_CHANNEL 8

/**
 * @brief GPIO configuration structure
 */
typedef struct {
    gpio_num_t pin;             /**< GPIO pin number */
    gpio_mode_t mode;           /**< GPIO mode (input/output) */
    gpio_pull_mode_t pull;      /**< Pull-up/pull-down configuration */
    gpio_int_type_t intr_type;  /**< Interrupt type */
    bool invert;                /**< Invert signal */
} hal_gpio_config_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    uart_port_t port;           /**< UART port number */
    int tx_pin;                 /**< TX pin number */
    int rx_pin;                 /**< RX pin number */
    int rts_pin;                /**< RTS pin number (-1 if not used) */
    int cts_pin;                /**< CTS pin number (-1 if not used) */
    uint32_t baud_rate;         /**< Baud rate */
    uart_word_length_t data_bits; /**< Data bits */
    uart_parity_t parity;       /**< Parity */
    uart_stop_bits_t stop_bits; /**< Stop bits */
    uart_hw_flowcontrol_t flow_ctrl; /**< Flow control */
    uint8_t rx_flow_ctrl_thresh; /**< RX flow control threshold */
    size_t rx_buffer_size;      /**< RX buffer size */
    size_t tx_buffer_size;      /**< TX buffer size */
} hal_uart_config_t;

/**
 * @brief SPI configuration structure
 */
typedef struct {
    spi_host_device_t host;     /**< SPI host */
    int mosi_pin;               /**< MOSI pin number */
    int miso_pin;               /**< MISO pin number */
    int sclk_pin;               /**< SCLK pin number */
    int cs_pin;                 /**< CS pin number */
    uint32_t clock_speed;       /**< Clock speed in Hz */
    uint8_t mode;               /**< SPI mode (0-3) */
    uint8_t bit_order;          /**< Bit order (MSB/LSB first) */
    size_t queue_size;          /**< Transaction queue size */
} hal_spi_config_t;

/**
 * @brief PWM configuration structure
 */
typedef struct {
    ledc_channel_t channel;     /**< PWM channel */
    gpio_num_t pin;             /**< GPIO pin number */
    ledc_timer_t timer;         /**< Timer number */
    uint32_t frequency;         /**< PWM frequency in Hz */
    ledc_timer_bit_t resolution; /**< PWM resolution */
    uint32_t duty_cycle;        /**< Initial duty cycle (0-max) */
    bool invert;                /**< Invert output */
} hal_pwm_config_t;

/**
 * @brief ADC configuration structure
 */
typedef struct {
    adc_unit_t unit;            /**< ADC unit */
    adc_channel_t channel;      /**< ADC channel */
    adc_atten_t attenuation;    /**< Attenuation */
    adc_bitwidth_t bitwidth;    /**< Bit width */
} hal_adc_config_t;

/**
 * @brief Hardware HAL status structure
 */
typedef struct {
    bool initialized;           /**< HAL initialization status */
    uint32_t gpio_count;        /**< Number of configured GPIO pins */
    uint32_t uart_count;        /**< Number of configured UART ports */
    uint32_t spi_count;         /**< Number of configured SPI hosts */
    uint32_t pwm_count;         /**< Number of configured PWM channels */
    uint32_t adc_count;         /**< Number of configured ADC channels */
} hal_status_t;

/**
 * @brief Initialize the Hardware HAL
 * 
 * This function initializes the hardware abstraction layer and sets up
 * the necessary resources for hardware access.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hardware_hal_init(void);

/**
 * @brief Deinitialize the Hardware HAL
 * 
 * This function cleans up all resources and deinitializes the hardware
 * abstraction layer.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hardware_hal_deinit(void);

/**
 * @brief Check if Hardware HAL is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool hardware_hal_is_initialized(void);

/**
 * @brief Get Hardware HAL status
 * 
 * @param status Pointer to status structure
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hardware_hal_get_status(hal_status_t *status);

/* ============================================================================
 * GPIO Functions
 * ============================================================================ */

/**
 * @brief Configure a GPIO pin
 * 
 * @param config GPIO configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_gpio_configure(const hal_gpio_config_t *config);

/**
 * @brief Set GPIO pin level
 * 
 * @param pin GPIO pin number
 * @param level Logic level (0 or 1)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_gpio_set_level(gpio_num_t pin, uint32_t level);

/**
 * @brief Get GPIO pin level
 * 
 * @param pin GPIO pin number
 * @param level Pointer to store the level
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_gpio_get_level(gpio_num_t pin, uint32_t *level);

/**
 * @brief Toggle GPIO pin level
 * 
 * @param pin GPIO pin number
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_gpio_toggle(gpio_num_t pin);

/* ============================================================================
 * UART Functions
 * ============================================================================ */

/**
 * @brief Configure a UART port
 * 
 * @param config UART configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_uart_configure(const hal_uart_config_t *config);

/**
 * @brief Write data to UART
 * 
 * @param port UART port number
 * @param data Data buffer
 * @param length Data length
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes written, or -1 on error
 */
int hal_uart_write(uart_port_t port, const void *data, size_t length, uint32_t timeout_ms);

/**
 * @brief Read data from UART
 * 
 * @param port UART port number
 * @param data Data buffer
 * @param length Buffer size
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes read, or -1 on error
 */
int hal_uart_read(uart_port_t port, void *data, size_t length, uint32_t timeout_ms);

/**
 * @brief Check if UART has data available
 * 
 * @param port UART port number
 * @param available Pointer to store available byte count
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_uart_available(uart_port_t port, size_t *available);

/* ============================================================================
 * SPI Functions
 * ============================================================================ */

/**
 * @brief Configure SPI bus
 * 
 * @param config SPI configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_spi_configure(const hal_spi_config_t *config);

/**
 * @brief Perform SPI transaction
 * 
 * @param host SPI host
 * @param tx_data Transmit data buffer
 * @param rx_data Receive data buffer (can be NULL)
 * @param length Data length in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_spi_transfer(spi_host_device_t host, const void *tx_data, 
                          void *rx_data, size_t length, uint32_t timeout_ms);

/* ============================================================================
 * PWM Functions
 * ============================================================================ */

/**
 * @brief Configure PWM channel
 * 
 * @param config PWM configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_pwm_configure(const hal_pwm_config_t *config);

/**
 * @brief Set PWM duty cycle
 * 
 * @param channel PWM channel
 * @param duty_cycle Duty cycle value
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_pwm_set_duty(ledc_channel_t channel, uint32_t duty_cycle);

/**
 * @brief Set PWM frequency
 * 
 * @param timer PWM timer
 * @param frequency Frequency in Hz
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_pwm_set_frequency(ledc_timer_t timer, uint32_t frequency);

/**
 * @brief Start PWM output
 * 
 * @param channel PWM channel
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_pwm_start(ledc_channel_t channel);

/**
 * @brief Stop PWM output
 * 
 * @param channel PWM channel
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_pwm_stop(ledc_channel_t channel);

/* ============================================================================
 * ADC Functions
 * ============================================================================ */

/**
 * @brief Configure ADC channel
 * 
 * @param config ADC configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_adc_configure(const hal_adc_config_t *config);

/**
 * @brief Read ADC value
 * 
 * @param unit ADC unit
 * @param channel ADC channel
 * @param raw_value Pointer to store raw ADC value
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_adc_read_raw(adc_unit_t unit, adc_channel_t channel, int *raw_value);

/**
 * @brief Convert raw ADC value to voltage
 * 
 * @param unit ADC unit
 * @param channel ADC channel
 * @param raw_value Raw ADC value
 * @param voltage_mv Pointer to store voltage in millivolts
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_adc_raw_to_voltage(adc_unit_t unit, adc_channel_t channel, 
                                int raw_value, int *voltage_mv);

/**
 * @brief Read ADC voltage directly
 * 
 * @param unit ADC unit
 * @param channel ADC channel
 * @param voltage_mv Pointer to store voltage in millivolts
 * @return ESP_OK on success, error code on failure
 */
esp_err_t hal_adc_read_voltage(adc_unit_t unit, adc_channel_t channel, int *voltage_mv);

#ifdef __cplusplus
}
#endif