/**
 * @file device_controller.h
 * @brief Device Controller Component for robOS
 *
 * This component provides control functionality for external devices like AGX
 * and LPMU, including power management, reset operations, and status
 * monitoring.
 *
 * Features:
 * - AGX device power control (on/off/reset/recovery)
 * - LPMU device power control (toggle/reset)
 * - Device status monitoring
 * - GPIO-based device control
 * - Power state management
 *
 * Hardware Configuration:
 * - AGX Power Pin: GPIO 3 - AGX power control (LOW=ON, HIGH=OFF)
 * - AGX Reset Pin: GPIO 1 - AGX reset control
 * - AGX Recovery Pin: GPIO 40 - AGX recovery mode control
 * - LPMU Power Button Pin: GPIO 46 - LPMU power button control
 * - LPMU Reset Pin: GPIO 2 - LPMU reset control
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#ifndef DEVICE_CONTROLLER_H
#define DEVICE_CONTROLLER_H

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

#define DEVICE_CONTROLLER_TAG "DEV_CTRL" ///< Log tag

// Hardware pin definitions
#define AGX_POWER_PIN 3       ///< AGX power control pin (GPIO3)
#define AGX_RESET_PIN 1       ///< AGX reset pin (GPIO1)
#define AGX_RECOVERY_PIN 40   ///< AGX recovery mode pin (GPIO40)
#define LPMU_POWER_BTN_PIN 46 ///< LPMU power button pin (GPIO46)
#define LPMU_RESET_PIN 2      ///< LPMU reset pin (GPIO2)

// Timing configurations
#define AGX_RESET_PULSE_MS 1000 ///< AGX reset pulse duration (ms)
#define LPMU_POWER_PULSE_MS 300 ///< LPMU power button pulse duration (ms)
#define LPMU_RESET_PULSE_MS 300 ///< LPMU reset pulse duration (ms)

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * @brief Power state enumeration
 */
typedef enum {
  POWER_STATE_OFF = 0,    ///< Device is powered off
  POWER_STATE_ON = 1,     ///< Device is powered on
  POWER_STATE_UNKNOWN = 2 ///< Power state is unknown
} power_state_t;

/**
 * @brief Device configuration structure
 */
typedef struct {
  bool auto_start_lpmu; ///< Auto-start LPMU on system boot
} device_config_t;

/**
 * @brief Device status structure
 */
typedef struct {
  bool initialized;               ///< Initialization status
  power_state_t agx_power_state;  ///< AGX power state
  power_state_t lpmu_power_state; ///< LPMU power state
  uint32_t agx_operations_count;  ///< AGX operation count
  uint32_t lpmu_operations_count; ///< LPMU operation count
} device_status_t;

/* ============================================================================
 * Public Function Declarations
 * ============================================================================
 */

/**
 * @brief Initialize device controller component
 *
 * This function initializes the device controller and configures all
 * necessary GPIO pins for AGX and LPMU control.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Initialization failed
 *     - ESP_ERR_INVALID_STATE: GPIO controller not available
 */
esp_err_t device_controller_init(void);

/**
 * @brief Deinitialize device controller component
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Deinitialization failed
 */
esp_err_t device_controller_deinit(void);

/**
 * @brief Check if device controller is initialized
 *
 * @return true if initialized, false otherwise
 */
bool device_controller_is_initialized(void);

// ==================== AGX Device Control ====================

/**
 * @brief Power on AGX device
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t device_controller_agx_power_on(void);

/**
 * @brief Power off AGX device
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t device_controller_agx_power_off(void);

/**
 * @brief Reset AGX device
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t device_controller_agx_reset(void);

/**
 * @brief Enter AGX recovery mode
 *
 * This function puts the AGX device into recovery mode and switches
 * the USB MUX to AGX for recovery operations.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: Operation failed
 */
esp_err_t device_controller_agx_enter_recovery_mode(void);

/**
 * @brief Get AGX power state
 *
 * @param state Pointer to store power state
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 */
esp_err_t device_controller_agx_get_power_state(power_state_t *state);

// ==================== LPMU Device Control ====================

/**
 * @brief Toggle LPMU power state
 *
 * This function simulates pressing the LPMU power button to toggle
 * the power state between on and off.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t device_controller_lpmu_power_toggle(void);

/**
 * @brief Reset LPMU device
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t device_controller_lpmu_reset(void);

/**
 * @brief Get LPMU power state
 *
 * @param state Pointer to store power state
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 */
esp_err_t device_controller_lpmu_get_power_state(power_state_t *state);

// ==================== Utility Functions ====================

/**
 * @brief Get power state name string
 *
 * @param state Power state
 * @return Power state name string
 */
const char *device_controller_get_power_state_name(power_state_t state);

/**
 * @brief Get device controller status
 *
 * @param status Pointer to store status information
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 */
esp_err_t device_controller_get_status(device_status_t *status);

// ==================== Configuration Management ====================

/**
 * @brief Get default device configuration
 *
 * @return Default configuration structure
 */
device_config_t device_controller_get_default_config(void);

/**
 * @brief Load device configuration from NVS
 *
 * @param config Pointer to store loaded configuration
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_ERR_NOT_FOUND: Configuration not found, using defaults
 *     - ESP_FAIL: Load failed
 */
esp_err_t device_controller_load_config(device_config_t *config);

/**
 * @brief Save device configuration to NVS
 *
 * @param config Configuration to save
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_FAIL: Save failed
 */
esp_err_t device_controller_save_config(const device_config_t *config);

/**
 * @brief Set LPMU auto-start configuration
 *
 * @param auto_start True to enable auto-start, false to disable
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Configuration save failed
 */
esp_err_t device_controller_set_lpmu_auto_start(bool auto_start);

/**
 * @brief Get LPMU auto-start configuration
 *
 * @param auto_start Pointer to store auto-start setting
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 */
esp_err_t device_controller_get_lpmu_auto_start(bool *auto_start);

/**
 * @brief Load configuration and handle LPMU auto-start
 *
 * This function should be called after config_manager is initialized.
 * It will load the device configuration and auto-start LPMU if configured.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Configuration load or auto-start failed
 */
esp_err_t device_controller_post_config_init(void);

// ==================== Testing Functions ====================

/**
 * @brief Test AGX power control functionality
 *
 * @return
 *     - ESP_OK: Test passed
 *     - ESP_FAIL: Test failed
 */
esp_err_t device_controller_test_agx_power(void);

/**
 * @brief Test LPMU power control functionality
 *
 * @return
 *     - ESP_OK: Test passed
 *     - ESP_FAIL: Test failed
 */
esp_err_t device_controller_test_lpmu_power(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_CONTROLLER_H