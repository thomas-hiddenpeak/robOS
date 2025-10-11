/**
 * @file usb_mux_controller.h
 * @brief USB MUX Controller Component for robOS
 *
 * This component provides USB-C interface switching functionality for the robOS
 * system, supporting switching between ESP32S3, AGX, and LPMU connection
 * targets.
 *
 * Features:
 * - USB-C interface switching control
 * - Support for ESP32S3/AGX/LPMU targets
 * - GPIO-based MUX control
 * - State management and persistence
 * - Status monitoring and logging
 *
 * Hardware Configuration:
 * - MUX1 Pin: GPIO 8 - USB MUX1 selection control
 * - MUX2 Pin: GPIO 48 - USB MUX2 selection control
 *
 * MUX Control Logic:
 * - ESP32S3: mux1=0, mux2=0 (default)
 * - AGX:     mux1=1, mux2=0
 * - LPMU:    mux1=1, mux2=1
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#ifndef USB_MUX_CONTROLLER_H
#define USB_MUX_CONTROLLER_H

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

#define USB_MUX_CONTROLLER_TAG "USB_MUX" ///< Log tag
#define USB_MUX1_PIN 8                   ///< USB MUX1 selection pin (GPIO8)
#define USB_MUX2_PIN 48                  ///< USB MUX2 selection pin (GPIO48)

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * @brief USB MUX target enumeration
 */
typedef enum {
  USB_MUX_TARGET_ESP32S3 = 0, /**< ESP32S3 target */
  USB_MUX_TARGET_AGX = 1,     /**< AGX target */
  USB_MUX_TARGET_LPMU = 2,    /**< LPMU target */
  USB_MUX_TARGET_MAX          /**< Maximum target count */
} usb_mux_target_t;

/**
 * @brief USB MUX status structure
 */
typedef struct {
  bool initialized;                ///< Initialization status
  usb_mux_target_t current_target; ///< Current USB MUX target
  uint32_t switch_count;           ///< Total number of switches
  uint32_t last_switch_time;       ///< Last switch timestamp (ticks)
} usb_mux_status_t;

/**
 * @brief USB MUX configuration structure
 */
typedef struct {
  usb_mux_target_t default_target; ///< Default target on initialization
  bool auto_restore;               ///< Auto restore target on init
  uint32_t switch_delay_ms;        ///< Delay between MUX pin changes
} usb_mux_config_t;

/* ============================================================================
 * Public Function Declarations
 * ============================================================================
 */

/**
 * @brief Initialize USB MUX controller component
 *
 * This function initializes the USB MUX controller with default configuration.
 * Default target is set to ESP32S3.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Initialization failed
 *     - ESP_ERR_INVALID_STATE: GPIO controller not available
 */
esp_err_t usb_mux_controller_init(void);

/**
 * @brief Initialize USB MUX controller with custom configuration
 *
 * @param config Pointer to USB MUX configuration
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_FAIL: Initialization failed
 *     - ESP_ERR_INVALID_STATE: GPIO controller not available
 */
esp_err_t usb_mux_controller_init_with_config(const usb_mux_config_t *config);

/**
 * @brief Deinitialize USB MUX controller component
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Deinitialization failed
 */
esp_err_t usb_mux_controller_deinit(void);

/**
 * @brief Check if USB MUX controller is initialized
 *
 * @return true if initialized, false otherwise
 */
bool usb_mux_controller_is_initialized(void);

/**
 * @brief Set USB MUX target device
 *
 * This function switches the USB-C interface to the specified target device.
 * The switch is performed by setting the appropriate MUX pin states.
 *
 * @param target Target device to switch to
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid target
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t usb_mux_controller_set_target(usb_mux_target_t target);

/**
 * @brief Get current USB MUX target device
 *
 * @param target Pointer to store current target
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 */
esp_err_t usb_mux_controller_get_target(usb_mux_target_t *target);

/**
 * @brief Get USB MUX target name string
 *
 * @param target USB MUX target
 * @return Target name string, "Unknown" if invalid
 */
const char *usb_mux_controller_get_target_name(usb_mux_target_t target);

/**
 * @brief Get USB MUX controller status
 *
 * @param status Pointer to store status information
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid pointer
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 */
esp_err_t usb_mux_controller_get_status(usb_mux_status_t *status);

/**
 * @brief Validate USB MUX target
 *
 * @param target Target to validate
 * @return
 *     - ESP_OK: Valid target
 *     - ESP_ERR_INVALID_ARG: Invalid target
 */
esp_err_t usb_mux_controller_validate_target(usb_mux_target_t target);

/**
 * @brief Reset USB MUX to default target
 *
 * This function resets the USB MUX to the default target (ESP32S3).
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: GPIO operation failed
 */
esp_err_t usb_mux_controller_reset_to_default(void);

/**
 * @brief Get default USB MUX configuration
 *
 * @return Pointer to default configuration structure
 */
const usb_mux_config_t *usb_mux_controller_get_default_config(void);

/**
 * @brief Save current USB MUX configuration to NVS
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_FAIL: Save operation failed
 */
esp_err_t usb_mux_controller_save_config(void);

/**
 * @brief Load USB MUX configuration from NVS
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Controller not initialized
 *     - ESP_ERR_NOT_FOUND: No saved configuration found
 *     - ESP_FAIL: Load operation failed
 */
esp_err_t usb_mux_controller_load_config(void);

/**
 * @brief Verify current GPIO pin states match expected target
 *
 * This function reads the actual GPIO pin states and verifies they match
 * the expected states for the current target. Useful for debugging hardware
 * issues and ensuring switches are working correctly.
 *
 * @param target_verified Pointer to store whether target is verified (optional)
 * @return
 *     - ESP_OK: GPIO states match expected target
 *     - ESP_ERR_INVALID_STATE: Controller not initialized or mismatch detected
 *     - ESP_FAIL: GPIO read operation failed
 */
esp_err_t usb_mux_controller_verify_target(bool *target_verified);

#ifdef __cplusplus
}
#endif

#endif // USB_MUX_CONTROLLER_H