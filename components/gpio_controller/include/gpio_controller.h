/**
 * @file gpio_controller.h
 * @brief GPIO Controller Component for robOS
 *
 * This component provides general GPIO operations for the robOS system,
 * supporting output control and input reading while avoiding state
 * interference.
 *
 * Features:
 * - Safe GPIO output control (high/low)
 * - Input mode reading with automatic mode switching
 * - State interference prevention design
 * - Pin configuration management
 * - Error handling and logging
 *
 * GPIO Safety Principles:
 * - Output Control: Use gpio <pin> high|low to set output state
 * - Input Reading: Use gpio <pin> input to switch to input mode and read
 * - Avoid reading state in output mode to prevent GPIO interference
 * - Critical operations (like recovery mode) completely avoid state validation
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#ifndef GPIO_CONTROLLER_H
#define GPIO_CONTROLLER_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Macros
 * ============================================================================
 */

#define GPIO_CONTROLLER_TAG "GPIO_CTRL" ///< Log tag
#define GPIO_MAX_PIN_NUM 48             ///< Maximum GPIO pin number for ESP32S3

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * @brief GPIO state enumeration
 */
typedef enum {
  GPIO_STATE_LOW = 0, ///< Low level (0V)
  GPIO_STATE_HIGH = 1 ///< High level (3.3V)
} gpio_state_t;

/**
 * @brief GPIO mode enumeration
 */
typedef enum {
  GPIO_CTRL_MODE_INPUT = 0, ///< Input mode
  GPIO_CTRL_MODE_OUTPUT = 1 ///< Output mode
} gpio_ctrl_mode_t;

/**
 * @brief GPIO pin configuration structure
 */
typedef struct {
  uint8_t pin;           ///< GPIO pin number
  gpio_ctrl_mode_t mode; ///< Current GPIO mode
  gpio_state_t state;    ///< Current GPIO state (for output mode)
  bool configured;       ///< Configuration status
} gpio_pin_config_t;

/* ============================================================================
 * Public Function Declarations
 * ============================================================================
 */

/**
 * @brief Initialize GPIO controller component
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Initialization failed
 */
esp_err_t gpio_controller_init(void);

/**
 * @brief Deinitialize GPIO controller component
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Deinitialization failed
 */
esp_err_t gpio_controller_deinit(void);

/**
 * @brief Check if GPIO controller is initialized
 *
 * @return true if initialized, false otherwise
 */
bool gpio_controller_is_initialized(void);

/**
 * @brief Set GPIO pin as output and set its level
 *
 * This function configures the GPIO pin as output mode and sets the specified
 * level. It's safe to call this function multiple times on the same pin.
 *
 * @param pin GPIO pin number
 * @param state GPIO state (high/low)
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pin number or state
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t gpio_controller_set_output(uint8_t pin, gpio_state_t state);

/**
 * @brief Read GPIO pin value in input mode
 *
 * This function switches the GPIO pin to input mode and reads its current
 * level. This is the safe way to read GPIO state without interfering with
 * output operations.
 *
 * @param pin GPIO pin number
 * @param state Pointer to store the read state
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pin number or null pointer
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t gpio_controller_read_input(uint8_t pin, gpio_state_t *state);

/**
 * @brief Get GPIO pin configuration
 *
 * @param pin GPIO pin number
 * @param config Pointer to store pin configuration
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pin number or null pointer
 *     - ESP_ERR_NOT_FOUND: Pin not configured
 */
esp_err_t gpio_controller_get_pin_config(uint8_t pin,
                                         gpio_pin_config_t *config);

/**
 * @brief Reset GPIO pin to default state
 *
 * This function resets the GPIO pin to its default state and removes it from
 * the internal configuration tracking.
 *
 * @param pin GPIO pin number
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pin number
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t gpio_controller_reset_pin(uint8_t pin);

/**
 * @brief Validate GPIO pin number
 *
 * @param pin GPIO pin number to validate
 * @return
 *     - ESP_OK: Valid pin
 *     - ESP_ERR_INVALID_ARG: Invalid pin
 */
esp_err_t gpio_controller_validate_pin(uint8_t pin);

/**
 * @brief Get component status and statistics
 *
 * @param configured_pins_count Pointer to store number of configured pins
 * (optional)
 * @param total_operations Pointer to store total operations count (optional)
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Component not initialized
 */
esp_err_t gpio_controller_get_status(uint32_t *configured_pins_count,
                                     uint32_t *total_operations);

#ifdef __cplusplus
}
#endif

#endif // GPIO_CONTROLLER_H
