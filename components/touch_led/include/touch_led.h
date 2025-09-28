/**
 * @file touch_led.h
 * @brief Touch-responsive LED controller using WS2812
 * @author robOS Team
 * @date 2025-09-28
 * 
 * This component provides touch-responsive LED control using WS2812 addressable LEDs.
 * It integrates touch sensor detection with visual feedback through LED animations.
 */

#ifndef TOUCH_LED_H
#define TOUCH_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touch LED configuration structure
 */
typedef struct {
    gpio_num_t led_gpio;           /**< GPIO pin for WS2812 LED data line */
    gpio_num_t touch_gpio;         /**< GPIO pin for touch sensor */
    uint16_t led_count;            /**< Number of LEDs in the strip */
    uint32_t max_brightness;       /**< Maximum brightness (0-255) */
    uint32_t touch_threshold;      /**< Touch detection threshold */
    bool touch_invert;             /**< Invert touch logic (true for active low) */
} touch_led_config_t;

/**
 * @brief RGB color structure
 */
typedef struct {
    uint8_t red;                   /**< Red component (0-255) */
    uint8_t green;                 /**< Green component (0-255) */
    uint8_t blue;                  /**< Blue component (0-255) */
} rgb_color_t;

/**
 * @brief LED animation modes
 */
typedef enum {
    TOUCH_LED_ANIM_NONE = 0,       /**< No animation */
    TOUCH_LED_ANIM_FADE,           /**< Fade in/out animation */
    TOUCH_LED_ANIM_RAINBOW,        /**< Rainbow color cycle */
    TOUCH_LED_ANIM_BREATHE,        /**< Breathing effect */
    TOUCH_LED_ANIM_PULSE,          /**< Pulse effect */
    TOUCH_LED_ANIM_WAVE,           /**< Wave effect */
    TOUCH_LED_ANIM_SPARKLE,        /**< Sparkle effect */
    TOUCH_LED_ANIM_MAX
} touch_led_animation_t;

/**
 * @brief Touch event types
 */
typedef enum {
    TOUCH_EVENT_NONE = 0,          /**< No touch event */
    TOUCH_EVENT_PRESS,             /**< Touch press detected */
    TOUCH_EVENT_RELEASE,           /**< Touch release detected */
    TOUCH_EVENT_LONG_PRESS,        /**< Long press detected */
    TOUCH_EVENT_DOUBLE_TAP,        /**< Double tap detected */
} touch_event_t;

/**
 * @brief Touch event callback function type
 * @param event Touch event type
 * @param duration Touch duration in milliseconds (for press/release events)
 */
typedef void (*touch_event_callback_t)(touch_event_t event, uint32_t duration);

/**
 * @brief Touch LED saved configuration structure
 * Used for persistent storage of LED state and preferences
 */
typedef struct {
    uint8_t version;                    /**< Configuration version for compatibility */
    bool is_enabled;                    /**< LED system enabled state */
    uint8_t brightness;                 /**< Current brightness (0-255) */
    rgb_color_t static_color;          /**< Current static color */
    bool has_static_color;             /**< Whether static color is set */
    touch_led_animation_t animation;   /**< Current animation mode */
    uint8_t animation_speed;           /**< Animation speed (1-255) */
    rgb_color_t animation_primary;     /**< Animation primary color */
    rgb_color_t animation_secondary;   /**< Animation secondary color */
    bool animation_running;            /**< Animation running state */
    bool touch_enabled;                /**< Touch detection enabled */
    uint32_t touch_threshold;          /**< Touch sensitivity threshold */
} touch_led_saved_config_t;

/**
 * @brief Initialize the touch LED system
 * @param config Configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_init(const touch_led_config_t *config);

/**
 * @brief Deinitialize the touch LED system
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_deinit(void);

/**
 * @brief Set LED color for a specific LED
 * @param led_index LED index (0 to led_count-1)
 * @param color RGB color
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_set_color(uint16_t led_index, rgb_color_t color);

/**
 * @brief Set color for all LEDs
 * @param color RGB color
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_set_all_color(rgb_color_t color);

/**
 * @brief Set LED brightness (0-255)
 * @param brightness Brightness level
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_set_brightness(uint8_t brightness);

/**
 * @brief Clear all LEDs (turn off)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_clear(void);

/**
 * @brief Update LED strip (commit changes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_update(void);

/**
 * @brief Start LED animation
 * @param animation Animation mode
 * @param speed Animation speed (1-255, higher is faster)
 * @param primary_color Primary color for animation
 * @param secondary_color Secondary color for animation (if applicable)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_start_animation(touch_led_animation_t animation, 
                                   uint8_t speed,
                                   rgb_color_t primary_color,
                                   rgb_color_t secondary_color);

/**
 * @brief Stop current animation
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_stop_animation(void);

/**
 * @brief Register touch event callback
 * @param callback Callback function
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_register_callback(touch_event_callback_t callback);

/**
 * @brief Get current touch state
 * @return true if touched, false otherwise
 */
bool touch_led_is_touched(void);

/**
 * @brief Get touch sensor raw value
 * @return Touch sensor raw reading
 */
uint32_t touch_led_get_touch_value(void);

/**
 * @brief Enable/disable touch detection
 * @param enable true to enable, false to disable
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_set_touch_enable(bool enable);

/**
 * @brief Set touch detection threshold
 * @param threshold New threshold value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_set_touch_threshold(uint32_t threshold);

/**
 * @brief Get LED strip status and statistics
 * @param led_count_out Pointer to store LED count
 * @param brightness_out Pointer to store current brightness
 * @param animation_out Pointer to store current animation mode
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_get_status(uint16_t *led_count_out, 
                              uint8_t *brightness_out,
                              touch_led_animation_t *animation_out);

/**
 * @brief Register touch LED console commands
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_register_commands(void);

/**
 * @brief Save current touch LED configuration to persistent storage
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_save_config(void);

/**
 * @brief Load touch LED configuration from persistent storage
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no config found, other error code on failure
 */
esp_err_t touch_led_load_config(void);

/**
 * @brief Reset touch LED configuration to factory defaults
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_led_reset_config(void);

/**
 * @brief Predefined colors for convenience
 */
extern const rgb_color_t TOUCH_LED_COLOR_RED;
extern const rgb_color_t TOUCH_LED_COLOR_GREEN;
extern const rgb_color_t TOUCH_LED_COLOR_BLUE;
extern const rgb_color_t TOUCH_LED_COLOR_WHITE;
extern const rgb_color_t TOUCH_LED_COLOR_YELLOW;
extern const rgb_color_t TOUCH_LED_COLOR_CYAN;
extern const rgb_color_t TOUCH_LED_COLOR_MAGENTA;
extern const rgb_color_t TOUCH_LED_COLOR_ORANGE;
extern const rgb_color_t TOUCH_LED_COLOR_PURPLE;
extern const rgb_color_t TOUCH_LED_COLOR_OFF;

#ifdef __cplusplus
}
#endif

#endif // TOUCH_LED_H