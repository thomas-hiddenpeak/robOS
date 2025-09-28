/**
 * @file board_led.h
 * @brief Board LED controller using WS2812 LED array
 * @author robOS Team
 * @date 2025-09-29
 * 
 * This component provides control for the onboard 28-LED WS2812 array on GPIO 42.
 * It supports individual LED control, global control, animations, and configuration management.
 */

#ifndef BOARD_LED_H
#define BOARD_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

#define BOARD_LED_GPIO_PIN      GPIO_NUM_42     /**< GPIO pin for WS2812 LED data line */
#define BOARD_LED_COUNT         28              /**< Number of LEDs in the array */
#define BOARD_LED_MAX_BRIGHTNESS 255           /**< Maximum brightness value */

/* Configuration version for NVS storage */
#define BOARD_LED_CONFIG_VERSION     1
#define BOARD_LED_CONFIG_NAMESPACE   "board_led"
#define BOARD_LED_CONFIG_KEY         "config"

/* ============================================================================
 * Data Structures and Enums
 * ============================================================================ */

/**
 * @brief RGB color structure
 */
typedef struct {
    uint8_t red;                   /**< Red component (0-255) */
    uint8_t green;                 /**< Green component (0-255) */
    uint8_t blue;                  /**< Blue component (0-255) */
} board_led_color_t;

/**
 * @brief LED animation modes
 */
typedef enum {
    BOARD_LED_ANIM_NONE = 0,       /**< No animation */
    BOARD_LED_ANIM_FADE,           /**< Fade in/out animation */
    BOARD_LED_ANIM_RAINBOW,        /**< Rainbow color cycle */
    BOARD_LED_ANIM_BREATHE,        /**< Breathing effect */
    BOARD_LED_ANIM_WAVE,           /**< Wave effect across LEDs */
    BOARD_LED_ANIM_CHASE,          /**< Chase/running light effect */
    BOARD_LED_ANIM_TWINKLE,        /**< Random twinkling effect */
    BOARD_LED_ANIM_FIRE,           /**< Fire flicker effect */
    BOARD_LED_ANIM_PULSE,          /**< Pulse effect */
    BOARD_LED_ANIM_GRADIENT,       /**< Gradient transition between colors */
    BOARD_LED_ANIM_RAINBOW_WAVE,   /**< Rainbow wave effect */
    BOARD_LED_ANIM_BRIGHTNESS_WAVE, /**< Brightness gradient wave */
    BOARD_LED_ANIM_COLOR_WIPE,     /**< Color wipe/fill effect */
    BOARD_LED_ANIM_SPARKLE,        /**< Sparkle effect with fading */
    BOARD_LED_ANIM_MAX             /**< Maximum animation value */
} board_led_animation_t;

/**
 * @brief Board LED configuration structure for persistence
 */
typedef struct {
    uint16_t version;              /**< Configuration version */
    bool is_enabled;               /**< LED system enabled state */
    uint8_t brightness;            /**< Global brightness (0-255) */
    board_led_color_t static_color; /**< Static color when not animating */
    bool has_static_color;         /**< Whether static color is set */
    board_led_animation_t animation; /**< Current animation type */
    uint8_t animation_speed;       /**< Animation speed (0-255) */
    board_led_color_t animation_primary; /**< Primary animation color */
    board_led_color_t animation_secondary; /**< Secondary animation color */
    bool animation_running;        /**< Animation state */
} board_led_saved_config_t;

/* ============================================================================
 * Predefined Colors
 * ============================================================================ */

#define BOARD_LED_COLOR_OFF      ((board_led_color_t){0, 0, 0})
#define BOARD_LED_COLOR_BLACK    ((board_led_color_t){0, 0, 0})
#define BOARD_LED_COLOR_WHITE    ((board_led_color_t){255, 255, 255})
#define BOARD_LED_COLOR_RED      ((board_led_color_t){255, 0, 0})
#define BOARD_LED_COLOR_GREEN    ((board_led_color_t){0, 255, 0})
#define BOARD_LED_COLOR_BLUE     ((board_led_color_t){0, 0, 255})
#define BOARD_LED_COLOR_YELLOW   ((board_led_color_t){255, 255, 0})
#define BOARD_LED_COLOR_CYAN     ((board_led_color_t){0, 255, 255})
#define BOARD_LED_COLOR_MAGENTA  ((board_led_color_t){255, 0, 255})
#define BOARD_LED_COLOR_ORANGE   ((board_led_color_t){255, 165, 0})
#define BOARD_LED_COLOR_PURPLE   ((board_led_color_t){128, 0, 128})
#define BOARD_LED_COLOR_PINK     ((board_led_color_t){255, 192, 203})

/* ============================================================================
 * Core API Functions
 * ============================================================================ */

/**
 * @brief Initialize the board LED system
 * 
 * Initializes the WS2812 LED strip, creates necessary tasks and mutexes.
 * Must be called before any other board LED functions.
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Already initialized
 *     - ESP_ERR_NO_MEM: Failed to allocate memory
 *     - ESP_FAIL: Other initialization failure
 */
esp_err_t board_led_init(void);

/**
 * @brief Deinitialize the board LED system
 * 
 * Cleans up resources, stops animations, and releases GPIO.
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 */
esp_err_t board_led_deinit(void);

/**
 * @brief Check if board LED system is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool board_led_is_initialized(void);

/* ============================================================================
 * LED Control Functions
 * ============================================================================ */

/**
 * @brief Set color of a specific LED
 * 
 * @param index LED index (0 to BOARD_LED_COUNT-1)
 * @param color RGB color to set
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_INVALID_ARG: Invalid LED index
 *     - ESP_ERR_TIMEOUT: Mutex timeout
 */
esp_err_t board_led_set_pixel(uint16_t index, board_led_color_t color);

/**
 * @brief Set color of all LEDs
 * 
 * @param color RGB color to set for all LEDs
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_TIMEOUT: Mutex timeout
 */
esp_err_t board_led_set_all_color(board_led_color_t color);

/**
 * @brief Set global brightness
 * 
 * @param brightness Brightness level (0-255)
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_TIMEOUT: Mutex timeout
 */
esp_err_t board_led_set_brightness(uint8_t brightness);

/**
 * @brief Get current brightness
 * 
 * @return Current brightness (0-255), or 0 if not initialized
 */
uint8_t board_led_get_brightness(void);

/**
 * @brief Clear all LEDs (turn off)
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_TIMEOUT: Mutex timeout
 */
esp_err_t board_led_clear(void);

/**
 * @brief Update LED display
 * 
 * Refreshes the LED strip to show current colors.
 * Must be called after setting colors to make them visible.
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_TIMEOUT: Mutex timeout
 */
esp_err_t board_led_update(void);

/* ============================================================================
 * Animation Functions
 * ============================================================================ */

/**
 * @brief Start LED animation
 * 
 * @param animation Animation type
 * @param speed Animation speed (0-255, higher = faster)
 * @param primary_color Primary color for animation
 * @param secondary_color Secondary color for animation (if needed)
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_INVALID_ARG: Invalid animation type
 *     - ESP_ERR_NO_MEM: Failed to create animation task
 */
esp_err_t board_led_start_animation(board_led_animation_t animation, uint8_t speed,
                                   board_led_color_t primary_color, board_led_color_t secondary_color);

/**
 * @brief Stop current animation
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized or no animation running
 */
esp_err_t board_led_stop_animation(void);

/**
 * @brief Check if animation is running
 * 
 * @return true if animation is running, false otherwise
 */
bool board_led_is_animation_running(void);

/**
 * @brief Get current animation type
 * 
 * @return Current animation type, or BOARD_LED_ANIM_NONE if no animation
 */
board_led_animation_t board_led_get_current_animation(void);

/* ============================================================================
 * Configuration Management Functions
 * ============================================================================ */

/**
 * @brief Save current configuration to NVS flash
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_NO_MEM: Insufficient memory
 *     - ESP_FAIL: NVS write error
 */
esp_err_t board_led_save_config(void);

/**
 * @brief Load configuration from NVS flash
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_ERR_NOT_FOUND: No saved configuration
 *     - ESP_ERR_NOT_SUPPORTED: Version mismatch
 *     - ESP_FAIL: NVS read error
 */
esp_err_t board_led_load_config(void);

/**
 * @brief Reset configuration to defaults
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Not initialized
 *     - ESP_FAIL: NVS erase error
 */
esp_err_t board_led_reset_config(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Convert HSV to RGB color
 * 
 * @param hue Hue (0-360)
 * @param saturation Saturation (0-100)
 * @param value Value/brightness (0-100)
 * @return RGB color
 */
board_led_color_t board_led_hsv_to_rgb(uint16_t hue, uint8_t saturation, uint8_t value);

/**
 * @brief Blend two colors
 * 
 * @param color1 First color
 * @param color2 Second color
 * @param ratio Blend ratio (0-255, 0=color1, 255=color2)
 * @return Blended color
 */
board_led_color_t board_led_blend_colors(board_led_color_t color1, board_led_color_t color2, uint8_t ratio);

/**
 * @brief Console command handler for board LED
 * 
 * This is called by the LED command dispatcher to handle board LED commands.
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return ESP_OK on success, error code on failure
 */
esp_err_t board_led_console_handler(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // BOARD_LED_H