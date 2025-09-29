/**
 * @file color_correction.h
 * @brief Color Correction for WS2812 LED Matrix
 * 
 * This component provides comprehensive color correction capabilities for LED matrices,
 * including white point correction, gamma correction, brightness enhancement, and 
 * saturation enhancement.
 * 
 * @version 1.0
 * @date 2024
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Include console commands
#include "color_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Color correction configuration structure
 */
typedef struct {
    bool enabled;                   /*!< Enable/disable color correction */
    
    // White point correction
    struct {
        bool enabled;               /*!< Enable white point correction */
        float red_scale;            /*!< Red channel scale factor (0.0 - 2.0) */
        float green_scale;          /*!< Green channel scale factor (0.0 - 2.0) */
        float blue_scale;           /*!< Blue channel scale factor (0.0 - 2.0) */
    } white_point;
    
    // Gamma correction
    struct {
        bool enabled;               /*!< Enable gamma correction */
        float gamma;                /*!< Gamma value (0.1 - 4.0, default 2.2) */
    } gamma;
    
    // Brightness enhancement
    struct {
        bool enabled;               /*!< Enable brightness enhancement */
        float factor;               /*!< Brightness factor (0.0 - 2.0, default 1.0) */
    } brightness;
    
    // Saturation enhancement
    struct {
        bool enabled;               /*!< Enable saturation enhancement */
        float factor;               /*!< Saturation factor (0.0 - 2.0, default 1.0) */
    } saturation;
    
} color_correction_config_t;

/**
 * @brief RGB color structure
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} color_rgb_t;

/**
 * @brief HSL color structure
 */
typedef struct {
    float h;    /*!< Hue (0.0 - 360.0) */
    float s;    /*!< Saturation (0.0 - 1.0) */
    float l;    /*!< Lightness (0.0 - 1.0) */
} color_hsl_t;

/**
 * @brief Initialize color correction system
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid parameters
 *     - ESP_ERR_NO_MEM: Memory allocation failed
 */
esp_err_t color_correction_init(void);

/**
 * @brief Deinitialize color correction system
 * 
 * @return
 *     - ESP_OK: Success
 */
esp_err_t color_correction_deinit(void);

/**
 * @brief Set color correction configuration
 * 
 * @param config Pointer to configuration structure
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 */
esp_err_t color_correction_set_config(const color_correction_config_t *config);

/**
 * @brief Get current color correction configuration
 * 
 * @param config Pointer to store configuration
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: NULL pointer
 */
esp_err_t color_correction_get_config(color_correction_config_t *config);

/**
 * @brief Get default color correction configuration
 * 
 * @param config Pointer to store default configuration
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: NULL pointer
 */
esp_err_t color_correction_get_default_config(color_correction_config_t *config);

/**
 * @brief Apply color correction to a single RGB pixel
 * 
 * @param input Input RGB color
 * @param output Output RGB color after correction
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid parameters
 */
esp_err_t color_correction_apply_pixel(const color_rgb_t *input, color_rgb_t *output);

/**
 * @brief Apply color correction to an array of RGB pixels
 * 
 * @param input Input RGB array
 * @param output Output RGB array after correction
 * @param count Number of pixels
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid parameters
 */
esp_err_t color_correction_apply_array(const color_rgb_t *input, color_rgb_t *output, size_t count);

/**
 * @brief Enable or disable color correction
 * 
 * @param enabled true to enable, false to disable
 * @return
 *     - ESP_OK: Success
 */
esp_err_t color_correction_set_enabled(bool enabled);

/**
 * @brief Check if color correction is enabled
 * 
 * @return true if enabled, false if disabled
 */
bool color_correction_is_enabled(void);

/**
 * @brief Set white point correction parameters
 * 
 * @param enabled Enable white point correction
 * @param red_scale Red channel scale factor
 * @param green_scale Green channel scale factor
 * @param blue_scale Blue channel scale factor
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid scale factors
 */
esp_err_t color_correction_set_white_point(bool enabled, float red_scale, float green_scale, float blue_scale);

/**
 * @brief Set gamma correction parameters
 * 
 * @param enabled Enable gamma correction
 * @param gamma Gamma value
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid gamma value
 */
esp_err_t color_correction_set_gamma(bool enabled, float gamma);

/**
 * @brief Set brightness enhancement parameters
 * 
 * @param enabled Enable brightness enhancement
 * @param factor Brightness factor
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid brightness factor
 */
esp_err_t color_correction_set_brightness(bool enabled, float factor);

/**
 * @brief Set saturation enhancement parameters
 * 
 * @param enabled Enable saturation enhancement
 * @param factor Saturation factor
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid saturation factor
 */
esp_err_t color_correction_set_saturation(bool enabled, float factor);

/**
 * @brief Export color correction configuration to JSON file
 * 
 * @param file_path Path to the output JSON file on SD card
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid file path
 *     - ESP_FAIL: File operation failed
 */
esp_err_t color_correction_export_config(const char *file_path);

/**
 * @brief Import color correction configuration from JSON file
 * 
 * @param file_path Path to the input JSON file on SD card
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid file path
 *     - ESP_ERR_NOT_FOUND: File not found
 *     - ESP_ERR_INVALID_SIZE: File too large
 *     - ESP_FAIL: Parse error or file operation failed
 */
esp_err_t color_correction_import_config(const char *file_path);

/**
 * @brief Convert RGB to HSL
 * 
 * @param rgb Input RGB color
 * @param hsl Output HSL color
 */
void color_rgb_to_hsl(const color_rgb_t *rgb, color_hsl_t *hsl);

/**
 * @brief Convert HSL to RGB
 * 
 * @param hsl Input HSL color
 * @param rgb Output RGB color
 */
void color_hsl_to_rgb(const color_hsl_t *hsl, color_rgb_t *rgb);

/**
 * @brief Callback function type for configuration changes
 */
typedef void (*color_correction_change_callback_t)(void);

/**
 * @brief Register callback for configuration changes
 * 
 * @param callback Callback function to call when configuration changes
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 */
esp_err_t color_correction_register_change_callback(color_correction_change_callback_t callback);

#ifdef __cplusplus
}
#endif