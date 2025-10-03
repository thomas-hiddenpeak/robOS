/**
 * @file hardware_commands.h
 * @brief Hardware Commands Component for robOS
 *
 * This component provides hardware-related console commands for the robOS
 * system, including GPIO control and USB MUX switching functionality.
 *
 * Features:
 * - GPIO control commands (gpio <pin> high|low|input)
 * - USB MUX switching commands (usbmux esp32s3|agx|lpmu|status)
 * - Safe GPIO operations with state management
 * - Console command registration and handling
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#ifndef HARDWARE_COMMANDS_H
#define HARDWARE_COMMANDS_H

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

#define HARDWARE_COMMANDS_TAG "HW_CMDS" ///< Log tag

/* ============================================================================
 * Public Function Declarations
 * ============================================================================
 */

/**
 * @brief Initialize hardware commands component
 *
 * This function initializes the hardware commands component and registers
 * the gpio and usbmux commands with the console system.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Initialization failed
 *     - ESP_ERR_INVALID_STATE: Console or hardware components not ready
 */
esp_err_t hardware_commands_init(void);

/**
 * @brief Deinitialize hardware commands component
 *
 * This function deinitializes the hardware commands component and unregisters
 * the commands from the console system.
 *
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Deinitialization failed
 */
esp_err_t hardware_commands_deinit(void);

/**
 * @brief Check if hardware commands component is initialized
 *
 * @return true if initialized, false otherwise
 */
bool hardware_commands_is_initialized(void);

/**
 * @brief GPIO command handler
 *
 * Handles gpio command: gpio <pin> high|low|input
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Command execution failed
 */
esp_err_t hardware_cmd_gpio(int argc, char **argv);

/**
 * @brief USB MUX command handler
 *
 * Handles usbmux command: usbmux esp32s3|agx|lpmu|status
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Command execution failed
 */
esp_err_t hardware_cmd_usbmux(int argc, char **argv);

/**
 * @brief AGX device command handler
 *
 * Handles agx command: agx on|off|reset|recovery|status
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Command execution failed
 */
esp_err_t hardware_cmd_agx(int argc, char **argv);

/**
 * @brief LPMU device command handler
 *
 * Handles lpmu command: lpmu toggle|reset|status
 *
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Command execution failed
 */
esp_err_t hardware_cmd_lpmu(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_COMMANDS_H