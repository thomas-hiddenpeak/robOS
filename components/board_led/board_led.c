/**
 * @file board_led.c
 * @brief Board LED controller implementation
 * @author robOS Team
 * @date 2025-09-29
 */

#include "board_led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "config_manager.h"
#include "console_core.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================================
 * Private Constants and Macros
 * ============================================================================ */

static const char *TAG = "board_led";

#define BOARD_LED_ANIMATION_TASK_STACK_SIZE    4096
#define BOARD_LED_ANIMATION_TASK_PRIORITY      5
#define BOARD_LED_MUTEX_TIMEOUT_MS             100

/* ============================================================================
 * Private Data Structures
 * ============================================================================ */

/**
 * @brief Internal board LED state structure
 */
typedef struct {
    led_strip_handle_t led_strip;          /**< LED strip handle */
    SemaphoreHandle_t mutex;               /**< Mutex for thread safety */
    bool is_initialized;                   /**< Initialization state */
    
    // Current state
    uint8_t current_brightness;            /**< Current brightness (0-255) */
    board_led_color_t current_static_color; /**< Current static color */
    bool has_static_color;                 /**< Whether static color is set */
    
    // Animation state
    board_led_animation_t current_animation; /**< Current animation type */
    uint8_t animation_speed;               /**< Animation speed (0-255) */
    board_led_color_t animation_primary_color; /**< Primary animation color */
    board_led_color_t animation_secondary_color; /**< Secondary animation color */
    bool animation_running;                /**< Animation running state */
    TaskHandle_t animation_task_handle;    /**< Animation task handle */
    uint32_t animation_step;               /**< Current animation step */
} board_led_state_t;

/* ============================================================================
 * Private Variables
 * ============================================================================ */

static board_led_state_t s_board_led = {
    .led_strip = NULL,
    .mutex = NULL,
    .is_initialized = false,
    .current_brightness = BOARD_LED_MAX_BRIGHTNESS,
    .current_static_color = BOARD_LED_COLOR_OFF,
    .has_static_color = false,
    .current_animation = BOARD_LED_ANIM_NONE,
    .animation_speed = 50,
    .animation_primary_color = BOARD_LED_COLOR_BLUE,
    .animation_secondary_color = BOARD_LED_COLOR_OFF,
    .animation_running = false,
    .animation_task_handle = NULL,
    .animation_step = 0
};

/* ============================================================================
 * Private Function Declarations
 * ============================================================================ */

static void apply_brightness(board_led_color_t *color, uint8_t brightness);
static void animation_task(void *arg);
static void animate_fade(void);
static void animate_rainbow(void);
static void animate_breathe(void);
static void animate_wave(void);
static void animate_chase(void);
static void animate_twinkle(void);
static void animate_fire(void);
static void animate_pulse(void);
static void animate_gradient(void);
static void animate_rainbow_wave(void);
static void animate_brightness_wave(void);
static void animate_color_wipe(void);
static void animate_sparkle(void);

/* Console command functions */
esp_err_t board_led_console_handler(int argc, char **argv);
static void board_led_console_help(void);
// static esp_err_t board_led_console_animation(int argc, char **argv);
// static esp_err_t board_led_console_config(int argc, char **argv);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Apply brightness scaling to a color
 */
static void apply_brightness(board_led_color_t *color, uint8_t brightness)
{
    if (brightness == 0) {
        color->red = color->green = color->blue = 0;
        return;
    }
    
    if (brightness == 255) {
        return; // No scaling needed
    }
    
    color->red = (color->red * brightness) / 255;
    color->green = (color->green * brightness) / 255;
    color->blue = (color->blue * brightness) / 255;
}

/* ============================================================================
 * Core API Implementation
 * ============================================================================ */

esp_err_t board_led_init(void)
{
    if (s_board_led.is_initialized) {
        ESP_LOGW(TAG, "Board LED already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing board LED system (GPIO %d, %d LEDs)", 
             BOARD_LED_GPIO_PIN, BOARD_LED_COUNT);

    // Create mutex
    s_board_led.mutex = xSemaphoreCreateMutex();
    if (s_board_led.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Configure LED strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_LED_GPIO_PIN,
        .max_leds = BOARD_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_board_led.led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_board_led.mutex);
        s_board_led.mutex = NULL;
        return ret;
    }

    // Clear all LEDs initially
    ret = led_strip_clear(s_board_led.led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip: %s", esp_err_to_name(ret));
        led_strip_del(s_board_led.led_strip);
        vSemaphoreDelete(s_board_led.mutex);
        s_board_led.mutex = NULL;
        return ret;
    }

    s_board_led.is_initialized = true;
    
    // Load saved configuration
    esp_err_t load_ret = board_led_load_config();
    if (load_ret == ESP_OK) {
        ESP_LOGI(TAG, "Board LED configuration restored from saved settings");
    } else if (load_ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved board LED configuration found, using defaults");
    }

    // Console commands are handled through the touch_led dispatcher
    // No need to register a separate command here

    ESP_LOGI(TAG, "Board LED system initialized successfully");
    return ESP_OK;
}

esp_err_t board_led_deinit(void)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing board LED system");

    // Stop animation if running
    if (s_board_led.animation_running) {
        board_led_stop_animation();
    }

    // Clear LEDs
    if (s_board_led.led_strip) {
        led_strip_clear(s_board_led.led_strip);
        led_strip_refresh(s_board_led.led_strip);
        led_strip_del(s_board_led.led_strip);
        s_board_led.led_strip = NULL;
    }

    // Delete mutex
    if (s_board_led.mutex) {
        vSemaphoreDelete(s_board_led.mutex);
        s_board_led.mutex = NULL;
    }

    s_board_led.is_initialized = false;
    ESP_LOGI(TAG, "Board LED system deinitialized");
    
    return ESP_OK;
}

bool board_led_is_initialized(void)
{
    return s_board_led.is_initialized;
}

/**
 * @brief Internal function to set pixel without stopping animation
 */
static esp_err_t board_led_set_pixel_internal(uint16_t index, board_led_color_t color)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (index >= BOARD_LED_COUNT) {
        ESP_LOGE(TAG, "Invalid LED index: %d (max: %d)", index, BOARD_LED_COUNT - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // Apply brightness
    apply_brightness(&color, s_board_led.current_brightness);

    esp_err_t ret = led_strip_set_pixel(s_board_led.led_strip, index, 
                                       color.red, color.green, color.blue);
    
    return ret;
}

esp_err_t board_led_set_pixel(uint16_t index, board_led_color_t color)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (index >= BOARD_LED_COUNT) {
        ESP_LOGE(TAG, "Invalid LED index: %d (max: %d)", index, BOARD_LED_COUNT - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // Stop any running animation first when setting individual pixels
    if (s_board_led.animation_running) {
        board_led_stop_animation();
    }

    if (xSemaphoreTake(s_board_led.mutex, pdMS_TO_TICKS(BOARD_LED_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // When setting individual pixels, we lose the "all same color" state
    s_board_led.has_static_color = false;

    esp_err_t ret = board_led_set_pixel_internal(index, color);
    
    xSemaphoreGive(s_board_led.mutex);
    
    // Auto-save configuration when setting individual pixels
    board_led_save_config();
    
    return ret;
}

esp_err_t board_led_set_all_color(board_led_color_t color)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Stop any running animation first
    if (s_board_led.animation_running) {
        board_led_stop_animation();
    }

    if (xSemaphoreTake(s_board_led.mutex, pdMS_TO_TICKS(BOARD_LED_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Update static color state (store original color before brightness adjustment)
    s_board_led.current_static_color = color;
    s_board_led.has_static_color = true;

    // Apply brightness
    apply_brightness(&color, s_board_led.current_brightness);

    esp_err_t ret = ESP_OK;
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        esp_err_t err = led_strip_set_pixel(s_board_led.led_strip, i, 
                                           color.red, color.green, color.blue);
        if (err != ESP_OK) {
            ret = err;
        }
    }

    xSemaphoreGive(s_board_led.mutex);
    
    // Auto-save configuration when setting static color
    board_led_save_config();
    
    return ret;
}

esp_err_t board_led_set_brightness(uint8_t brightness)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_board_led.mutex, pdMS_TO_TICKS(BOARD_LED_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    s_board_led.current_brightness = brightness;
    
    // If we have a static color set, reapply it with the new brightness
    if (s_board_led.has_static_color) {
        board_led_color_t color = s_board_led.current_static_color;
        apply_brightness(&color, brightness);
        
        for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
            led_strip_set_pixel(s_board_led.led_strip, i, 
                               color.red, color.green, color.blue);
        }
        led_strip_refresh(s_board_led.led_strip);
    }
    
    xSemaphoreGive(s_board_led.mutex);

    ESP_LOGD(TAG, "Brightness set to %d", brightness);
    
    // Auto-save configuration
    board_led_save_config();
    
    return ESP_OK;
}

uint8_t board_led_get_brightness(void)
{
    if (!s_board_led.is_initialized) {
        return 0;
    }
    return s_board_led.current_brightness;
}

esp_err_t board_led_clear(void)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Stop any running animation first
    if (s_board_led.animation_running) {
        board_led_stop_animation();
    }

    if (xSemaphoreTake(s_board_led.mutex, pdMS_TO_TICKS(BOARD_LED_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear static color state when clearing LEDs
    s_board_led.has_static_color = false;
    
    esp_err_t ret = led_strip_clear(s_board_led.led_strip);
    xSemaphoreGive(s_board_led.mutex);

    // Auto-save configuration after clearing
    board_led_save_config();

    return ret;
}

esp_err_t board_led_update(void)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_board_led.mutex, pdMS_TO_TICKS(BOARD_LED_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = led_strip_refresh(s_board_led.led_strip);
    xSemaphoreGive(s_board_led.mutex);

    return ret;
}

/* ============================================================================
 * Animation System Implementation
 * ============================================================================ */

esp_err_t board_led_start_animation(board_led_animation_t animation, uint8_t speed,
                                   board_led_color_t primary_color, board_led_color_t secondary_color)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (animation >= BOARD_LED_ANIM_MAX) {
        ESP_LOGE(TAG, "Invalid animation type: %d", animation);
        return ESP_ERR_INVALID_ARG;
    }

    // Stop existing animation
    if (s_board_led.animation_running) {
        board_led_stop_animation();
    }

    s_board_led.current_animation = animation;
    s_board_led.animation_speed = speed > 0 ? speed : 50;
    s_board_led.animation_primary_color = primary_color;
    s_board_led.animation_secondary_color = secondary_color;
    s_board_led.animation_step = 0;
    s_board_led.animation_running = true;
    
    // Clear static color state when starting animation
    s_board_led.has_static_color = false;

    if (animation != BOARD_LED_ANIM_NONE) {
        // Create animation task
        BaseType_t task_created = xTaskCreate(
            animation_task,
            "board_led_anim",
            BOARD_LED_ANIMATION_TASK_STACK_SIZE,
            NULL,
            BOARD_LED_ANIMATION_TASK_PRIORITY,
            &s_board_led.animation_task_handle
        );

        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create animation task");
            s_board_led.animation_running = false;
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "Started animation %d with speed %d", animation, speed);
    
    // Auto-save configuration
    board_led_save_config();
    
    return ESP_OK;
}

esp_err_t board_led_stop_animation(void)
{
    if (!s_board_led.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_board_led.animation_running = false;

    if (s_board_led.animation_task_handle) {
        vTaskDelete(s_board_led.animation_task_handle);
        s_board_led.animation_task_handle = NULL;
    }

    s_board_led.current_animation = BOARD_LED_ANIM_NONE;
    ESP_LOGI(TAG, "Animation stopped");
    
    // Auto-save configuration
    board_led_save_config();
    
    return ESP_OK;
}

bool board_led_is_animation_running(void)
{
    return s_board_led.animation_running;
}

board_led_animation_t board_led_get_current_animation(void)
{
    return s_board_led.current_animation;
}

/**
 * @brief Animation task implementation
 */
static void animation_task(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (s_board_led.animation_running) {
        switch (s_board_led.current_animation) {
            case BOARD_LED_ANIM_FADE:
                animate_fade();
                break;
            case BOARD_LED_ANIM_RAINBOW:
                animate_rainbow();
                break;
            case BOARD_LED_ANIM_BREATHE:
                animate_breathe();
                break;
            case BOARD_LED_ANIM_WAVE:
                animate_wave();
                break;
            case BOARD_LED_ANIM_CHASE:
                animate_chase();
                break;
            case BOARD_LED_ANIM_TWINKLE:
                animate_twinkle();
                break;
            case BOARD_LED_ANIM_FIRE:
                animate_fire();
                break;
            case BOARD_LED_ANIM_PULSE:
                animate_pulse();
                break;
            case BOARD_LED_ANIM_GRADIENT:
                animate_gradient();
                break;
            case BOARD_LED_ANIM_RAINBOW_WAVE:
                animate_rainbow_wave();
                break;
            case BOARD_LED_ANIM_BRIGHTNESS_WAVE:
                animate_brightness_wave();
                break;
            case BOARD_LED_ANIM_COLOR_WIPE:
                animate_color_wipe();
                break;
            case BOARD_LED_ANIM_SPARKLE:
                animate_sparkle();
                break;
            default:
                s_board_led.animation_running = false;
                break;
        }
        
        // Update LED display
        board_led_update();
        
        // Increment animation step
        s_board_led.animation_step++;
        
        // Delay based on animation speed
        uint32_t delay_ms = 100 - (s_board_led.animation_speed * 90 / 255);
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(delay_ms));
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Fade animation implementation
 */
static void animate_fade(void)
{
    uint8_t fade_value = (uint8_t)((sin(s_board_led.animation_step * 0.1) + 1) * 127);
    board_led_color_t color = s_board_led.animation_primary_color;
    
    color.red = (color.red * fade_value) / 255;
    color.green = (color.green * fade_value) / 255;
    color.blue = (color.blue * fade_value) / 255;
    
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Rainbow animation implementation
 */
static void animate_rainbow(void)
{
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        uint16_t hue = (s_board_led.animation_step * 10 + i * 360 / BOARD_LED_COUNT) % 360;
        board_led_color_t color = board_led_hsv_to_rgb(hue, 100, 100);
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Breathe animation implementation
 */
static void animate_breathe(void)
{
    uint8_t breath_value = (uint8_t)((sin(s_board_led.animation_step * 0.2) + 1) * 127);
    board_led_color_t color = s_board_led.animation_primary_color;
    
    color.red = (color.red * breath_value) / 255;
    color.green = (color.green * breath_value) / 255;
    color.blue = (color.blue * breath_value) / 255;
    
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Wave animation implementation
 */
static void animate_wave(void)
{
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        uint8_t wave_value = (uint8_t)((sin((s_board_led.animation_step + i * 5) * 0.3) + 1) * 127);
        board_led_color_t color = board_led_blend_colors(
            s_board_led.animation_secondary_color,
            s_board_led.animation_primary_color,
            wave_value
        );
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Chase animation implementation
 */
static void animate_chase(void)
{
    uint16_t chase_pos = s_board_led.animation_step % BOARD_LED_COUNT;
    
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        if (i == chase_pos) {
            board_led_set_pixel_internal(i, s_board_led.animation_primary_color);
        } else if ((i + 1) % BOARD_LED_COUNT == chase_pos || (i + BOARD_LED_COUNT - 1) % BOARD_LED_COUNT == chase_pos) {
            board_led_color_t dim_color = s_board_led.animation_primary_color;
            dim_color.red /= 3;
            dim_color.green /= 3;
            dim_color.blue /= 3;
            board_led_set_pixel_internal(i, dim_color);
        } else {
            board_led_set_pixel_internal(i, s_board_led.animation_secondary_color);
        }
    }
}

/**
 * @brief Twinkle animation implementation
 */
static void animate_twinkle(void)
{
    // Randomly update some LEDs
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        if ((esp_random() % 100) < 10) { // 10% chance to change
            if ((esp_random() % 2) == 0) {
                board_led_set_pixel_internal(i, s_board_led.animation_primary_color);
            } else {
                board_led_set_pixel_internal(i, s_board_led.animation_secondary_color);
            }
        }
    }
}

/**
 * @brief Fire animation implementation
 */
static void animate_fire(void)
{
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        uint8_t flicker = 200 + (esp_random() % 56); // 200-255
        board_led_color_t color = {
            .red = flicker,
            .green = flicker / 3,
            .blue = 0
        };
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Pulse animation implementation
 */
static void animate_pulse(void)
{
    uint8_t pulse_value = 0;
    uint32_t cycle = s_board_led.animation_step % 100;
    
    if (cycle < 50) {
        pulse_value = (cycle * 255) / 50;
    } else {
        pulse_value = ((100 - cycle) * 255) / 50;
    }
    
    board_led_color_t color = s_board_led.animation_primary_color;
    color.red = (color.red * pulse_value) / 255;
    color.green = (color.green * pulse_value) / 255;
    color.blue = (color.blue * pulse_value) / 255;
    
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        board_led_set_pixel_internal(i, color);
    }
}

/* ============================================================================
 * Utility Functions Implementation
 * ============================================================================ */

board_led_color_t board_led_hsv_to_rgb(uint16_t hue, uint8_t saturation, uint8_t value)
{
    board_led_color_t color = {0, 0, 0};
    
    if (saturation == 0) {
        color.red = color.green = color.blue = value * 255 / 100;
        return color;
    }
    
    hue = hue % 360;
    uint8_t region = hue / 60;
    uint8_t remainder = (hue - (region * 60)) * 6;
    
    uint8_t p = (value * (100 - saturation)) / 100;
    uint8_t q = (value * (100 - ((saturation * remainder) / 255))) / 100;
    uint8_t t = (value * (100 - ((saturation * (255 - remainder)) / 255))) / 100;
    
    switch (region) {
        case 0:
            color.red = value * 255 / 100;
            color.green = t * 255 / 100;
            color.blue = p * 255 / 100;
            break;
        case 1:
            color.red = q * 255 / 100;
            color.green = value * 255 / 100;
            color.blue = p * 255 / 100;
            break;
        case 2:
            color.red = p * 255 / 100;
            color.green = value * 255 / 100;
            color.blue = t * 255 / 100;
            break;
        case 3:
            color.red = p * 255 / 100;
            color.green = q * 255 / 100;
            color.blue = value * 255 / 100;
            break;
        case 4:
            color.red = t * 255 / 100;
            color.green = p * 255 / 100;
            color.blue = value * 255 / 100;
            break;
        default:
            color.red = value * 255 / 100;
            color.green = p * 255 / 100;
            color.blue = q * 255 / 100;
            break;
    }
    
    return color;
}

board_led_color_t board_led_blend_colors(board_led_color_t color1, board_led_color_t color2, uint8_t ratio)
{
    board_led_color_t result;
    
    result.red = ((color1.red * (255 - ratio)) + (color2.red * ratio)) / 255;
    result.green = ((color1.green * (255 - ratio)) + (color2.green * ratio)) / 255;
    result.blue = ((color1.blue * (255 - ratio)) + (color2.blue * ratio)) / 255;
    
    return result;
}

/* ============================================================================
 * Creative Color Band Animation Functions
 * ============================================================================ */

/**
 * @brief Gradient transition animation between two colors
 */
static void animate_gradient(void)
{
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        uint8_t ratio = (i * 255) / (BOARD_LED_COUNT - 1);
        board_led_color_t color = board_led_blend_colors(
            s_board_led.animation_primary_color, 
            s_board_led.animation_secondary_color, 
            ratio
        );
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Rainbow wave animation - moving rainbow pattern
 */
static void animate_rainbow_wave(void)
{
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        uint16_t hue = (s_board_led.animation_step * 5 + i * 360 / BOARD_LED_COUNT) % 360;
        board_led_color_t color = board_led_hsv_to_rgb(hue, 100, 100);
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Brightness gradient wave animation
 */
static void animate_brightness_wave(void)
{
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        // Create a sine wave pattern for brightness
        float position = (float)i / (BOARD_LED_COUNT - 1) * 2 * M_PI;
        float wave_offset = (float)s_board_led.animation_step * 0.1;
        uint8_t brightness = (uint8_t)((sin(position + wave_offset) + 1) * 127);
        
        board_led_color_t color = s_board_led.animation_primary_color;
        color.red = (color.red * brightness) / 255;
        color.green = (color.green * brightness) / 255;
        color.blue = (color.blue * brightness) / 255;
        
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Color wipe animation - fills LEDs sequentially
 */
static void animate_color_wipe(void)
{
    uint16_t progress = (s_board_led.animation_step / 5) % (BOARD_LED_COUNT * 2);
    
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        board_led_color_t color;
        
        if (progress < BOARD_LED_COUNT) {
            // Fill phase
            if (i <= progress) {
                color = s_board_led.animation_primary_color;
            } else {
                color = BOARD_LED_COLOR_BLACK;
            }
        } else {
            // Clear phase
            uint16_t clear_progress = progress - BOARD_LED_COUNT;
            if (i <= clear_progress) {
                color = BOARD_LED_COLOR_BLACK;
            } else {
                color = s_board_led.animation_primary_color;
            }
        }
        
        board_led_set_pixel_internal(i, color);
    }
}

/**
 * @brief Sparkle animation with fading trails
 */
static void animate_sparkle(void)
{
    static uint8_t sparkle_brightness[BOARD_LED_COUNT] = {0};
    
    // Fade existing sparkles
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        if (sparkle_brightness[i] > 0) {
            sparkle_brightness[i] = (sparkle_brightness[i] > 10) ? sparkle_brightness[i] - 10 : 0;
        }
    }
    
    // Add new sparkles randomly
    if ((s_board_led.animation_step % 3) == 0) {
        uint16_t sparkle_led = esp_random() % BOARD_LED_COUNT;
        sparkle_brightness[sparkle_led] = 255;
    }
    
    // Apply sparkle effect
    for (uint16_t i = 0; i < BOARD_LED_COUNT; i++) {
        board_led_color_t color = s_board_led.animation_primary_color;
        color.red = (color.red * sparkle_brightness[i]) / 255;
        color.green = (color.green * sparkle_brightness[i]) / 255;
        color.blue = (color.blue * sparkle_brightness[i]) / 255;
        
        board_led_set_pixel_internal(i, color);
    }
}

/* ============================================================================
 * Configuration Management Functions (Simplified)
 * ============================================================================ */

esp_err_t board_led_save_config(void)
{
    if (!s_board_led.is_initialized) {
        ESP_LOGE(TAG, "Board LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize config manager if not already done
    if (!config_manager_is_initialized()) {
        config_manager_config_t config_mgr_config = config_manager_get_default_config();
        esp_err_t config_ret = config_manager_init(&config_mgr_config);
        if (config_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize config manager: %s", esp_err_to_name(config_ret));
            return config_ret;
        }
    }

    // Prepare configuration data
    board_led_saved_config_t saved_config = {
        .version = BOARD_LED_CONFIG_VERSION,
        .is_enabled = s_board_led.is_initialized,
        .brightness = s_board_led.current_brightness,
        .static_color = s_board_led.has_static_color ? s_board_led.current_static_color : (board_led_color_t){0, 0, 0},
        .has_static_color = s_board_led.has_static_color,
        .animation = s_board_led.current_animation,
        .animation_speed = s_board_led.animation_speed,
        .animation_primary = s_board_led.animation_primary_color,
        .animation_secondary = s_board_led.animation_secondary_color,
        .animation_running = s_board_led.animation_running
    };

    // Save configuration to NVS
    esp_err_t ret = config_manager_set(BOARD_LED_CONFIG_NAMESPACE, BOARD_LED_CONFIG_KEY,
                                      CONFIG_TYPE_BLOB, &saved_config, sizeof(saved_config));
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Board LED configuration saved (brightness: %d, animation: %d, running: %s)",
                saved_config.brightness, saved_config.animation,
                saved_config.animation_running ? "yes" : "no");
        config_manager_commit();
    } else {
        ESP_LOGE(TAG, "Failed to save board LED config: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t board_led_load_config(void)
{
    if (!s_board_led.is_initialized) {
        ESP_LOGE(TAG, "Board LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize config manager if not already done
    if (!config_manager_is_initialized()) {
        config_manager_config_t config_mgr_config = config_manager_get_default_config();
        esp_err_t config_ret = config_manager_init(&config_mgr_config);
        if (config_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize config manager: %s", esp_err_to_name(config_ret));
            return config_ret;
        }
    }

    // Load configuration from NVS
    board_led_saved_config_t saved_config;
    size_t config_size = sizeof(saved_config);
    esp_err_t ret = config_manager_get(BOARD_LED_CONFIG_NAMESPACE, BOARD_LED_CONFIG_KEY,
                                      CONFIG_TYPE_BLOB, &saved_config, &config_size);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved board LED configuration found: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify configuration version
    if (saved_config.version != BOARD_LED_CONFIG_VERSION) {
        ESP_LOGW(TAG, "Board LED configuration version mismatch (saved: %d, expected: %d)",
                saved_config.version, BOARD_LED_CONFIG_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    // Apply loaded configuration
    s_board_led.current_brightness = saved_config.brightness;
    s_board_led.has_static_color = saved_config.has_static_color;
    if (saved_config.has_static_color) {
        s_board_led.current_static_color = saved_config.static_color;
    }
    s_board_led.animation_speed = saved_config.animation_speed;
    s_board_led.animation_primary_color = saved_config.animation_primary;
    s_board_led.animation_secondary_color = saved_config.animation_secondary;

    // Restore animation state if it was running
    if (saved_config.animation_running && saved_config.animation != BOARD_LED_ANIM_NONE) {
        board_led_start_animation(saved_config.animation, saved_config.animation_speed,
                                 saved_config.animation_primary, saved_config.animation_secondary);
    } else if (saved_config.has_static_color) {
        // Restore static color if no animation
        board_led_set_all_color(saved_config.static_color);
        board_led_update();
    }

    ESP_LOGI(TAG, "Board LED configuration loaded (brightness: %d, animation: %d, static: %s)",
            saved_config.brightness, saved_config.animation, 
            saved_config.has_static_color ? "yes" : "no");

    return ESP_OK;
}

esp_err_t board_led_reset_config(void)
{
    if (!s_board_led.is_initialized) {
        ESP_LOGE(TAG, "Board LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize config manager if not already done
    if (!config_manager_is_initialized()) {
        config_manager_config_t config_mgr_config = config_manager_get_default_config();
        esp_err_t config_ret = config_manager_init(&config_mgr_config);
        if (config_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize config manager: %s", esp_err_to_name(config_ret));
            return config_ret;
        }
    }

    // Remove configuration from NVS
    esp_err_t ret = config_manager_delete(BOARD_LED_CONFIG_NAMESPACE, BOARD_LED_CONFIG_KEY);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Board LED configuration reset successfully");
        config_manager_commit();
        
        // Reset to default values
        s_board_led.current_brightness = BOARD_LED_MAX_BRIGHTNESS;
        s_board_led.has_static_color = false;
        s_board_led.animation_speed = 50;
        s_board_led.animation_primary_color = BOARD_LED_COLOR_BLUE;
        s_board_led.animation_secondary_color = BOARD_LED_COLOR_RED;
        
        // Stop any running animation
        if (s_board_led.animation_running) {
            board_led_stop_animation();
        }
        
        // Clear LEDs
        board_led_clear();
        board_led_update();
        
    } else {
        ESP_LOGE(TAG, "Failed to reset board LED config: %s", esp_err_to_name(ret));
    }

    return ret;
}

/* ============================================================================
 * Console Command Functions (Simplified)
 * ============================================================================ */

esp_err_t board_led_console_handler(int argc, char **argv)
{
    if (argc < 2) {
        board_led_console_help();
        return ESP_OK;
    }

    if (strcasecmp(argv[1], "board") == 0) {
        if (argc < 3) {
            printf("Usage: led board <command>\n");
            return ESP_OK;
        } else if (strcasecmp(argv[2], "help") == 0) {
            board_led_console_help();
            return ESP_OK;
        } else if (strcasecmp(argv[2], "on") == 0) {
            board_led_set_all_color(BOARD_LED_COLOR_WHITE);
            board_led_update();
            printf("Board LED turned on\n");
            return ESP_OK;
        } else if (strcasecmp(argv[2], "off") == 0) {
            board_led_clear();
            board_led_update();
            printf("Board LED turned off\n");
            return ESP_OK;
        } else if (strcasecmp(argv[2], "all") == 0 && argc >= 6) {
            // Temporary simple implementation
            int r = atoi(argv[3]);
            int g = atoi(argv[4]);
            int b = atoi(argv[5]);
            board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
            board_led_set_all_color(color);
            board_led_update();
            printf("Set all LEDs to RGB(%d,%d,%d)\n", r, g, b);
            return ESP_OK;
        } else if (strcasecmp(argv[2], "set") == 0 && argc >= 7) {
            // Temporary simple implementation  
            int index = atoi(argv[3]);
            int r = atoi(argv[4]);
            int g = atoi(argv[5]);
            int b = atoi(argv[6]);
            board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
            board_led_set_pixel((uint16_t)index, color);
            board_led_update();
            printf("Set LED %d to RGB(%d,%d,%d)\n", index, r, g, b);
            return ESP_OK;
        } else if (strcasecmp(argv[2], "brightness") == 0 && argc >= 4) {
            // Temporary simple implementation
            int brightness = atoi(argv[3]);
            board_led_set_brightness((uint8_t)brightness);
            printf("Set brightness to %d\n", brightness);
            return ESP_OK;
        } else if (strcasecmp(argv[2], "clear") == 0) {
            board_led_clear();
            board_led_update();
            printf("Board LEDs cleared\n");
            return ESP_OK;
        } else if (strcasecmp(argv[2], "anim") == 0 && argc >= 4) {
            // Animation commands
            if (strcasecmp(argv[3], "stop") == 0) {
                board_led_stop_animation();
                printf("Animation stopped\n");
            } else if (strcasecmp(argv[3], "rainbow") == 0) {
                uint8_t speed = argc >= 5 ? (uint8_t)atoi(argv[4]) : 50;
                board_led_start_animation(BOARD_LED_ANIM_RAINBOW, speed, 
                                        BOARD_LED_COLOR_RED, BOARD_LED_COLOR_BLUE);
                printf("Started rainbow animation (speed: %d)\n", speed);
            } else if (strcasecmp(argv[3], "breathe") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_BREATHE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started breathe animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "chase") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_CHASE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started chase animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "fade") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_FADE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started fade animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "wave") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_WAVE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started wave animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "twinkle") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_TWINKLE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started twinkle animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "fire") == 0) {
                uint8_t speed = argc >= 5 ? (uint8_t)atoi(argv[4]) : 50;
                board_led_start_animation(BOARD_LED_ANIM_FIRE, speed, 
                                        BOARD_LED_COLOR_RED, BOARD_LED_COLOR_YELLOW);
                printf("Started fire animation (speed: %d)\n", speed);
            } else if (strcasecmp(argv[3], "pulse") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_PULSE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started pulse animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "gradient") == 0 && argc >= 10) {
                uint8_t speed = argc >= 11 ? (uint8_t)atoi(argv[10]) : 50;
                int r1 = atoi(argv[4]), g1 = atoi(argv[5]), b1 = atoi(argv[6]);
                int r2 = atoi(argv[7]), g2 = atoi(argv[8]), b2 = atoi(argv[9]);
                board_led_color_t color1 = {(uint8_t)r1, (uint8_t)g1, (uint8_t)b1};
                board_led_color_t color2 = {(uint8_t)r2, (uint8_t)g2, (uint8_t)b2};
                board_led_start_animation(BOARD_LED_ANIM_GRADIENT, speed, color1, color2);
                printf("Started gradient animation RGB(%d,%d,%d) to RGB(%d,%d,%d)\n", r1, g1, b1, r2, g2, b2);
            } else if (strcasecmp(argv[3], "rainbow_wave") == 0) {
                uint8_t speed = argc >= 5 ? (uint8_t)atoi(argv[4]) : 80;
                board_led_start_animation(BOARD_LED_ANIM_RAINBOW_WAVE, speed, 
                                        BOARD_LED_COLOR_RED, BOARD_LED_COLOR_BLUE);
                printf("Started rainbow wave animation (speed: %d)\n", speed);
            } else if (strcasecmp(argv[3], "brightness_wave") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 50;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_BRIGHTNESS_WAVE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started brightness wave animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "color_wipe") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 30;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_COLOR_WIPE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started color wipe animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else if (strcasecmp(argv[3], "sparkle") == 0 && argc >= 7) {
                uint8_t speed = argc >= 8 ? (uint8_t)atoi(argv[7]) : 70;
                int r = atoi(argv[4]);
                int g = atoi(argv[5]);
                int b = atoi(argv[6]);
                board_led_color_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                board_led_start_animation(BOARD_LED_ANIM_SPARKLE, speed, 
                                        color, BOARD_LED_COLOR_BLACK);
                printf("Started sparkle animation RGB(%d,%d,%d) (speed: %d)\n", r, g, b, speed);
            } else {
                printf("Unknown animation: %s\n", argv[3]);
                printf("Basic animations: rainbow, breathe, chase, fade, wave, twinkle, fire, pulse, stop\n");
                printf("Creative bands: gradient, rainbow_wave, brightness_wave, color_wipe, sparkle\n");
            }
            return ESP_OK;
        } else if (strcasecmp(argv[2], "config") == 0 && argc >= 4) {
            // Configuration commands
            if (strcasecmp(argv[3], "save") == 0) {
                esp_err_t ret = board_led_save_config();
                if (ret == ESP_OK) {
                    printf("Board LED configuration saved successfully\n");
                } else {
                    printf("Failed to save configuration: %s\n", esp_err_to_name(ret));
                }
            } else if (strcasecmp(argv[3], "load") == 0) {
                esp_err_t ret = board_led_load_config();
                if (ret == ESP_OK) {
                    printf("Board LED configuration loaded successfully\n");
                } else {
                    printf("Failed to load configuration: %s\n", esp_err_to_name(ret));
                }
            } else if (strcasecmp(argv[3], "reset") == 0) {
                esp_err_t ret = board_led_reset_config();
                if (ret == ESP_OK) {
                    printf("Board LED configuration reset to defaults\n");
                } else {
                    printf("Failed to reset configuration: %s\n", esp_err_to_name(ret));
                }
            } else {
                printf("Unknown config command: %s\n", argv[3]);
                printf("Available config commands: save, load, reset\n");
            }
            return ESP_OK;
        } else {
            printf("Unknown board LED command: %s\n", argv[2]);
            board_led_console_help();
        }
    } else {
        printf("Unknown command: %s\n", argv[1]);
        board_led_console_help();
    }

    return ESP_OK;
}

static void board_led_console_help(void)
{
    printf("Board LED Control Commands:\n");
    printf("  led board help                        - Show this help\n");
    printf("  led board on                          - Turn on all LEDs (white)\n");
    printf("  led board off                         - Turn off all LEDs\n");
    printf("  led board clear                       - Clear all LEDs\n");
    printf("  led board all <R> <G> <B>             - Set all LEDs to RGB color\n");
    printf("  led board set <index> <R> <G> <B>     - Set specific LED to RGB color\n");
    printf("  led board brightness <0-255>          - Set brightness\n");
    printf("\nAnimation Commands:\n");
    printf("  led board anim stop                   - Stop current animation\n");
    printf("  led board anim rainbow [speed]        - Rainbow animation (speed: 0-255)\n");
    printf("  led board anim breathe <R> <G> <B> [speed] - Breathing animation\n");
    printf("  led board anim chase <R> <G> <B> [speed]  - Chase animation\n");
    printf("  led board anim fade <R> <G> <B> [speed]   - Fade animation\n");
    printf("  led board anim wave <R> <G> <B> [speed]   - Wave animation\n");
    printf("  led board anim twinkle <R> <G> <B> [speed] - Twinkle animation\n");
    printf("  led board anim fire [speed]           - Fire effect animation\n");
    printf("  led board anim pulse <R> <G> <B> [speed]  - Pulse animation\n");
    printf("\nCreative Color Band Animations:\n");
    printf("  led board anim gradient <R1> <G1> <B1> <R2> <G2> <B2> [speed] - Gradient transition\n");
    printf("  led board anim rainbow_wave [speed]   - Moving rainbow pattern\n");
    printf("  led board anim brightness_wave <R> <G> <B> [speed] - Brightness wave\n");
    printf("  led board anim color_wipe <R> <G> <B> [speed] - Color wipe effect\n");
    printf("  led board anim sparkle <R> <G> <B> [speed] - Sparkle with fading\n");
    printf("\nConfiguration Commands:\n");
    printf("  led board config save                 - Save current settings to flash\n");
    printf("  led board config load                 - Load settings from flash\n");
    printf("  led board config reset                - Reset to default settings\n");
    printf("\n");
    printf("Examples:\n");
    printf("  led board all 255 0 0                 - Set all LEDs to red\n");
    printf("  led board set 0 0 255 0               - Set first LED to green\n");
    printf("  led board brightness 128              - Set brightness to 50%%\n");
    printf("  led board anim rainbow 100            - Fast rainbow animation\n");
    printf("  led board anim breathe 255 0 0 80     - Red breathing effect\n");
    printf("  led board anim gradient 255 0 0 0 0 255 60 - Red to blue gradient\n");
    printf("  led board anim rainbow_wave 120       - Fast rainbow wave\n");
    printf("  led board anim sparkle 255 255 255 80 - White sparkle effect\n");
    printf("  led board anim stop                   - Stop animation\n");
    printf("  led board config save                 - Save current configuration\n");
    printf("  led board clear                       - Clear LEDs and save state\n");
}