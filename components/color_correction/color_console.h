/**
 * @file color_console.h
 * @brief Color Correction Console Commands
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register color correction console commands
 * 
 * This function registers the "color" command group with sub-commands:
 * - color enable/disable
 * - color status
 * - color whitepoint <r> <g> <b>
 * - color gamma <value>
 * - color brightness <factor>
 * - color saturation <factor>
 * - color reset
 * - color save
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Console core not initialized
 *     - ESP_ERR_NO_MEM: No memory for command registration
 */
esp_err_t color_correction_register_console_commands(void);

#ifdef __cplusplus
}
#endif