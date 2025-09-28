/**
 * @file fan_controller.h
 * @brief Fan Controller Component for robOS
 * 
 * This component provides comprehensive fan control functionality including:
 * - PWM-based speed control
 * - Temperature-based automatic control
 * - Manual speed override
 * - Fan status monitoring
 * - Multiple fan support
 * 
 * @author robOS Team
 * @date 2025
 */

#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Macros
 * ============================================================================ */

#define FAN_CONTROLLER_MAX_FANS         4       ///< Maximum number of fans supported
#define FAN_CONTROLLER_MIN_SPEED        0       ///< Minimum fan speed (0%)
#define FAN_CONTROLLER_MAX_SPEED        100     ///< Maximum fan speed (100%)
#define FAN_CONTROLLER_PWM_FREQUENCY    25000   ///< PWM frequency for fan control (25kHz)
#define FAN_CONTROLLER_PWM_RESOLUTION   LEDC_TIMER_10_BIT ///< PWM resolution (10-bit)

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Fan control mode
 */
typedef enum {
    FAN_MODE_MANUAL = 0,        ///< Manual speed control
    FAN_MODE_AUTO_TEMP,         ///< Automatic temperature-based control
    FAN_MODE_AUTO_CURVE,        ///< Custom curve-based control
    FAN_MODE_OFF                ///< Fan disabled
} fan_mode_t;

/**
 * @brief Fan configuration structure
 */
typedef struct {
    uint8_t fan_id;             ///< Fan ID (0-3)
    int pwm_pin;                ///< PWM control pin
    int tach_pin;               ///< Tachometer input pin (-1 if not used)
    ledc_channel_t pwm_channel; ///< LEDC channel for PWM
    ledc_timer_t pwm_timer;     ///< LEDC timer for PWM
    fan_mode_t default_mode;    ///< Default control mode
    uint8_t default_speed;      ///< Default speed (0-100%)
    bool invert_pwm;            ///< True to invert PWM signal
} fan_config_t;

/**
 * @brief Fan status structure
 */
typedef struct {
    uint8_t fan_id;             ///< Fan ID
    bool enabled;               ///< Fan enabled status
    fan_mode_t mode;            ///< Current control mode
    uint8_t speed_percent;      ///< Current speed percentage (0-100%)
    uint32_t rpm;               ///< Current RPM (0 if tachometer not available)
    float temperature;          ///< Current temperature reference (°C)
    bool fault;                 ///< Fault status
} fan_status_t;

/**
 * @brief Temperature curve point for automatic control
 */
typedef struct {
    float temperature;          ///< Temperature in Celsius
    uint8_t speed_percent;      ///< Fan speed percentage (0-100%)
} fan_curve_point_t;

/**
 * @brief Fan controller configuration
 */
typedef struct {
    uint8_t num_fans;           ///< Number of fans to manage
    fan_config_t *fan_configs;  ///< Array of fan configurations
    bool enable_tachometer;     ///< Enable tachometer reading
    uint32_t update_interval_ms; ///< Status update interval (ms)
} fan_controller_config_t;

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * @brief Get default fan controller configuration
 * @return Default configuration structure
 */
fan_controller_config_t fan_controller_get_default_config(void);

/**
 * @brief Initialize fan controller
 * @param config Configuration parameters (NULL for default)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_init(const fan_controller_config_t *config);

/**
 * @brief Deinitialize fan controller
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_deinit(void);

/**
 * @brief Check if fan controller is initialized
 * @return true if initialized, false otherwise
 */
bool fan_controller_is_initialized(void);

/**
 * @brief Set fan speed manually
 * @param fan_id Fan ID (0-3)
 * @param speed_percent Speed percentage (0-100%)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_set_speed(uint8_t fan_id, uint8_t speed_percent);

/**
 * @brief Get fan speed
 * @param fan_id Fan ID (0-3)
 * @param speed_percent Pointer to store speed percentage
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_get_speed(uint8_t fan_id, uint8_t *speed_percent);

/**
 * @brief Set fan control mode
 * @param fan_id Fan ID (0-3)
 * @param mode Control mode
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_set_mode(uint8_t fan_id, fan_mode_t mode);

/**
 * @brief Get fan control mode
 * @param fan_id Fan ID (0-3)
 * @param mode Pointer to store control mode
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_get_mode(uint8_t fan_id, fan_mode_t *mode);

/**
 * @brief Enable or disable a fan
 * @param fan_id Fan ID (0-3)
 * @param enable True to enable, false to disable
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_enable(uint8_t fan_id, bool enable);

/**
 * @brief Check if fan is enabled
 * @param fan_id Fan ID (0-3)
 * @param enabled Pointer to store enabled status
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_is_enabled(uint8_t fan_id, bool *enabled);

/**
 * @brief Get fan status
 * @param fan_id Fan ID (0-3)
 * @param status Pointer to store fan status
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_get_status(uint8_t fan_id, fan_status_t *status);

/**
 * @brief Get all fans status
 * @param status_array Array to store fan status (must be sized for configured fans)
 * @param array_size Size of the status array
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_get_all_status(fan_status_t *status_array, uint8_t array_size);

/**
 * @brief Set temperature curve for automatic control
 * @param fan_id Fan ID (0-3)
 * @param curve_points Array of curve points
 * @param num_points Number of curve points
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_set_curve(uint8_t fan_id, const fan_curve_point_t *curve_points, uint8_t num_points);

/**
 * @brief Update temperature reference for automatic control
 * @param fan_id Fan ID (0-3)  
 * @param temperature Temperature in Celsius
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_update_temperature(uint8_t fan_id, float temperature);

/**
 * @brief Configure fan GPIO pin dynamically
 * @param fan_id Fan ID (0-3)
 * @param pwm_pin New PWM GPIO pin number
 * @param pwm_channel New PWM channel (optional, -1 to keep current)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_configure_gpio(uint8_t fan_id, int pwm_pin, int pwm_channel);

/**
 * @brief Configure fan GPIO pin dynamically
 * @param fan_id Fan ID (0-3)
 * @param pwm_pin New PWM GPIO pin number
 * @param pwm_channel New PWM channel (optional, -1 to keep current)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_configure_gpio(uint8_t fan_id, int pwm_pin, int pwm_channel);

/**
 * @brief Register fan control commands with console
 * @return ESP_OK on success, error code on failure
 */
esp_err_t fan_controller_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif // FAN_CONTROLLER_H