/**
 * @file touch_led.c
 * @brief Touch-responsive LED controller using WS2812
 * @author robOS Team
 * @date 2025-09-28
 */

#include "touch_led.h"
#include "driver/gpio.h"
#include "led_strip.h"
// GPIO based touch detection instead of touch sensor API
#include "board_led.h"
#include "config_manager.h"
#include "console_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

// Forward declarations for other LED subsystem console handlers
extern esp_err_t board_led_console_handler(int argc, char **argv);
extern int matrix_led_cmd_handler(int argc, char **argv);

// Function to check if matrix LED is initialized
extern bool matrix_led_is_initialized(void);

static const char *TAG = "touch_led";

// Configuration constants
#define TOUCH_LED_CONFIG_NAMESPACE "touch_led"
#define TOUCH_LED_CONFIG_KEY "config"
#define TOUCH_LED_CONFIG_VERSION 1

// Predefined colors
const rgb_color_t TOUCH_LED_COLOR_RED = {255, 0, 0};
const rgb_color_t TOUCH_LED_COLOR_GREEN = {0, 255, 0};
const rgb_color_t TOUCH_LED_COLOR_BLUE = {0, 0, 255};
const rgb_color_t TOUCH_LED_COLOR_WHITE = {255, 255, 255};
const rgb_color_t TOUCH_LED_COLOR_YELLOW = {255, 255, 0};
const rgb_color_t TOUCH_LED_COLOR_CYAN = {0, 255, 255};
const rgb_color_t TOUCH_LED_COLOR_MAGENTA = {255, 0, 255};
const rgb_color_t TOUCH_LED_COLOR_ORANGE = {255, 165, 0};
const rgb_color_t TOUCH_LED_COLOR_PURPLE = {128, 0, 128};
const rgb_color_t TOUCH_LED_COLOR_OFF = {0, 0, 0};

// Internal state structure
typedef struct {
  led_strip_handle_t led_strip;          // LED strip handle
  touch_led_config_t config;             // Configuration
  uint8_t current_brightness;            // Current brightness
  bool is_initialized;                   // Initialization flag
  bool touch_enabled;                    // Touch detection enabled
  bool is_touched;                       // Current touch state
  uint32_t last_touch_time;              // Last touch timestamp
  touch_event_callback_t event_callback; // Event callback

  // Animation state
  touch_led_animation_t current_animation;
  bool animation_running;
  uint8_t animation_speed;
  rgb_color_t animation_primary_color;
  rgb_color_t animation_secondary_color;
  uint32_t animation_step;

  // Static color state
  rgb_color_t current_static_color;
  bool has_static_color;

  // Task and synchronization
  TaskHandle_t animation_task_handle;
  TaskHandle_t touch_task_handle;
  SemaphoreHandle_t mutex;
} touch_led_state_t;

static touch_led_state_t s_touch_led = {0};

// Forward declarations
static void animation_task(void *arg);
static void touch_detection_task(void *arg);
static esp_err_t apply_brightness(rgb_color_t *color, uint8_t brightness);
static rgb_color_t hsv_to_rgb(float h, float s, float v);

esp_err_t touch_led_init(const touch_led_config_t *config) {
  if (!config || config->led_count == 0) {
    ESP_LOGE(TAG, "Invalid configuration");
    return ESP_ERR_INVALID_ARG;
  }

  if (s_touch_led.is_initialized) {
    ESP_LOGW(TAG, "Touch LED already initialized");
    return ESP_OK;
  }

  // Copy configuration
  memcpy(&s_touch_led.config, config, sizeof(touch_led_config_t));

  // Create mutex
  s_touch_led.mutex = xSemaphoreCreateMutex();
  if (!s_touch_led.mutex) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }

  // Initialize LED strip
  led_strip_config_t strip_config = {
      .strip_gpio_num = config->led_gpio,
      .max_leds = config->led_count,
      .led_pixel_format = LED_PIXEL_FORMAT_GRB,
      .led_model = LED_MODEL_WS2812,
      .flags.invert_out = false,
  };

  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000, // 10MHz
      .mem_block_symbols = 48,           // 16个LED，使用最小允许值
      .flags = {
          .with_dma = false,
      }};

  esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config,
                                           &s_touch_led.led_strip);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
    vSemaphoreDelete(s_touch_led.mutex);
    return ret;
  }

  // Initialize touch sensor if touch GPIO is specified
  if (config->touch_gpio != GPIO_NUM_NC) {
    // For ESP32S3, we'll use a simple GPIO-based touch detection
    // instead of the complex touch sensor API
    gpio_config_t touch_gpio_config = {.pin_bit_mask =
                                           (1ULL << config->touch_gpio),
                                       .mode = GPIO_MODE_INPUT,
                                       .pull_up_en = GPIO_PULLUP_ENABLE,
                                       .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                       .intr_type = GPIO_INTR_DISABLE};

    ret = gpio_config(&touch_gpio_config);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Touch GPIO config failed: %s", esp_err_to_name(ret));
    } else {
      s_touch_led.touch_enabled = true;

      // Create touch detection task
      BaseType_t task_ret =
          xTaskCreate(touch_detection_task, "touch_detect", 4096, NULL, 5,
                      &s_touch_led.touch_task_handle);

      if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create touch detection task");
        s_touch_led.touch_enabled = false;
      }
    }
  }

  // Set default values
  s_touch_led.current_brightness = config->max_brightness;
  s_touch_led.current_animation = TOUCH_LED_ANIM_NONE;
  s_touch_led.animation_running = false;
  s_touch_led.is_initialized = true;

  // Clear all LEDs initially
  touch_led_clear();
  touch_led_update();

  ESP_LOGI(TAG,
           "Touch LED initialized with %d LEDs on GPIO %d, touch on GPIO %d",
           config->led_count, config->led_gpio, config->touch_gpio);

  // Load saved configuration
  esp_err_t load_ret = touch_led_load_config();
  if (load_ret == ESP_OK) {
    ESP_LOGI(TAG, "Touch LED configuration restored from saved settings");
  }

  return ESP_OK;
}

esp_err_t touch_led_deinit(void) {
  if (!s_touch_led.is_initialized) {
    return ESP_OK;
  }

  // Stop animation
  touch_led_stop_animation();

  // Stop touch detection
  if (s_touch_led.touch_task_handle) {
    vTaskDelete(s_touch_led.touch_task_handle);
    s_touch_led.touch_task_handle = NULL;
  }

  // Reset touch GPIO
  if (s_touch_led.touch_enabled &&
      s_touch_led.config.touch_gpio != GPIO_NUM_NC) {
    gpio_reset_pin(s_touch_led.config.touch_gpio);
  }

  // Clear and delete LED strip
  touch_led_clear();
  touch_led_update();

  if (s_touch_led.led_strip) {
    led_strip_del(s_touch_led.led_strip);
    s_touch_led.led_strip = NULL;
  }

  // Delete mutex
  if (s_touch_led.mutex) {
    vSemaphoreDelete(s_touch_led.mutex);
    s_touch_led.mutex = NULL;
  }

  // Reset state
  memset(&s_touch_led, 0, sizeof(touch_led_state_t));

  ESP_LOGI(TAG, "Touch LED deinitialized");
  return ESP_OK;
}

esp_err_t touch_led_set_color(uint16_t led_index, rgb_color_t color) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (led_index >= s_touch_led.config.led_count) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_touch_led.mutex, portMAX_DELAY);

  // Apply brightness
  apply_brightness(&color, s_touch_led.current_brightness);

  esp_err_t ret = led_strip_set_pixel(s_touch_led.led_strip, led_index,
                                      color.red, color.green, color.blue);

  xSemaphoreGive(s_touch_led.mutex);
  return ret;
}

esp_err_t touch_led_set_all_color(rgb_color_t color) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_touch_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  // Update static color state (store original color before brightness
  // adjustment)
  s_touch_led.current_static_color = color;
  s_touch_led.has_static_color = true;

  // Apply brightness
  apply_brightness(&color, s_touch_led.current_brightness);

  esp_err_t ret = ESP_OK;
  for (uint16_t i = 0; i < s_touch_led.config.led_count; i++) {
    esp_err_t err = led_strip_set_pixel(s_touch_led.led_strip, i, color.red,
                                        color.green, color.blue);
    if (err != ESP_OK) {
      ret = err;
    }
  }

  xSemaphoreGive(s_touch_led.mutex);

  // Auto-save configuration when setting static color
  touch_led_save_config();

  return ret;
}

esp_err_t touch_led_set_brightness(uint8_t brightness) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_touch_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  s_touch_led.current_brightness = brightness;
  xSemaphoreGive(s_touch_led.mutex);

  ESP_LOGD(TAG, "Brightness set to %d", brightness);

  // Auto-save configuration
  touch_led_save_config();

  return ESP_OK;
}

esp_err_t touch_led_clear(void) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_touch_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  // Clear static color state when clearing LEDs
  s_touch_led.has_static_color = false;

  esp_err_t ret = led_strip_clear(s_touch_led.led_strip);
  xSemaphoreGive(s_touch_led.mutex);

  return ret;
}

esp_err_t touch_led_update(void) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_touch_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t ret = led_strip_refresh(s_touch_led.led_strip);
  xSemaphoreGive(s_touch_led.mutex);

  return ret;
}

esp_err_t touch_led_start_animation(touch_led_animation_t animation,
                                    uint8_t speed, rgb_color_t primary_color,
                                    rgb_color_t secondary_color) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (animation >= TOUCH_LED_ANIM_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  // Stop current animation
  touch_led_stop_animation();

  if (xSemaphoreTake(s_touch_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  s_touch_led.current_animation = animation;
  s_touch_led.animation_speed = speed > 0 ? speed : 50;
  s_touch_led.animation_primary_color = primary_color;
  s_touch_led.animation_secondary_color = secondary_color;
  s_touch_led.animation_step = 0;
  s_touch_led.animation_running = true;

  // Clear static color state when starting animation
  s_touch_led.has_static_color = false;

  xSemaphoreGive(s_touch_led.mutex);

  if (animation != TOUCH_LED_ANIM_NONE) {
    BaseType_t ret = xTaskCreate(animation_task, "led_animation", 4096, NULL, 4,
                                 &s_touch_led.animation_task_handle);

    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create animation task");
      s_touch_led.animation_running = false;
      return ESP_ERR_NO_MEM;
    }
  }

  ESP_LOGI(TAG, "Started animation %d with speed %d", animation, speed);

  // Auto-save configuration
  touch_led_save_config();

  return ESP_OK;
}

esp_err_t touch_led_stop_animation(void) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_touch_led.mutex, portMAX_DELAY);
  s_touch_led.animation_running = false;
  xSemaphoreGive(s_touch_led.mutex);

  if (s_touch_led.animation_task_handle) {
    vTaskDelete(s_touch_led.animation_task_handle);
    s_touch_led.animation_task_handle = NULL;
  }

  s_touch_led.current_animation = TOUCH_LED_ANIM_NONE;
  ESP_LOGI(TAG, "Animation stopped");

  // Auto-save configuration
  touch_led_save_config();

  return ESP_OK;
}

esp_err_t touch_led_register_callback(touch_event_callback_t callback) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_touch_led.mutex, portMAX_DELAY);
  s_touch_led.event_callback = callback;
  xSemaphoreGive(s_touch_led.mutex);

  return ESP_OK;
}

bool touch_led_is_touched(void) {
  if (!s_touch_led.is_initialized || !s_touch_led.touch_enabled) {
    return false;
  }

  return s_touch_led.is_touched;
}

uint32_t touch_led_get_touch_value(void) {
  if (!s_touch_led.is_initialized || !s_touch_led.touch_enabled) {
    return 0;
  }

  // For simple GPIO-based touch detection, return GPIO level
  // In a real implementation, you might want to use ADC to read analog values
  int gpio_level = gpio_get_level(s_touch_led.config.touch_gpio);
  return (uint32_t)gpio_level;
}

esp_err_t touch_led_set_touch_enable(bool enable) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_touch_led.mutex, portMAX_DELAY);
  s_touch_led.touch_enabled = enable;
  xSemaphoreGive(s_touch_led.mutex);

  ESP_LOGI(TAG, "Touch detection %s", enable ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t touch_led_set_touch_threshold(uint32_t threshold) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_touch_led.mutex, portMAX_DELAY);
  s_touch_led.config.touch_threshold = threshold;
  // Note: For GPIO-based touch detection, threshold is stored but not actively
  // used In a real implementation, you might configure ADC thresholds here
  xSemaphoreGive(s_touch_led.mutex);

  ESP_LOGI(TAG, "Touch threshold set to %lu", threshold);
  return ESP_OK;
}

esp_err_t touch_led_get_status(uint16_t *led_count_out, uint8_t *brightness_out,
                               touch_led_animation_t *animation_out) {
  if (!s_touch_led.is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (led_count_out) {
    *led_count_out = s_touch_led.config.led_count;
  }

  if (brightness_out) {
    *brightness_out = s_touch_led.current_brightness;
  }

  if (animation_out) {
    *animation_out = s_touch_led.current_animation;
  }

  return ESP_OK;
}

// Helper function to apply brightness to color
static esp_err_t apply_brightness(rgb_color_t *color, uint8_t brightness) {
  if (!color) {
    return ESP_ERR_INVALID_ARG;
  }

  color->red = (color->red * brightness) / 255;
  color->green = (color->green * brightness) / 255;
  color->blue = (color->blue * brightness) / 255;

  return ESP_OK;
}

// Convert HSV to RGB
static rgb_color_t hsv_to_rgb(float h, float s, float v) {
  rgb_color_t rgb = {0, 0, 0};

  int i = (int)(h / 60.0f) % 6;
  float f = (h / 60.0f) - i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - s * f);
  float t = v * (1.0f - s * (1.0f - f));

  switch (i) {
  case 0:
    rgb.red = (uint8_t)(v * 255);
    rgb.green = (uint8_t)(t * 255);
    rgb.blue = (uint8_t)(p * 255);
    break;
  case 1:
    rgb.red = (uint8_t)(q * 255);
    rgb.green = (uint8_t)(v * 255);
    rgb.blue = (uint8_t)(p * 255);
    break;
  case 2:
    rgb.red = (uint8_t)(p * 255);
    rgb.green = (uint8_t)(v * 255);
    rgb.blue = (uint8_t)(t * 255);
    break;
  case 3:
    rgb.red = (uint8_t)(p * 255);
    rgb.green = (uint8_t)(q * 255);
    rgb.blue = (uint8_t)(v * 255);
    break;
  case 4:
    rgb.red = (uint8_t)(t * 255);
    rgb.green = (uint8_t)(p * 255);
    rgb.blue = (uint8_t)(v * 255);
    break;
  case 5:
    rgb.red = (uint8_t)(v * 255);
    rgb.green = (uint8_t)(p * 255);
    rgb.blue = (uint8_t)(q * 255);
    break;
  }

  return rgb;
}

// Animation task implementation
static void animation_task(void *arg) {
  TickType_t last_wake_time = xTaskGetTickCount();

  while (s_touch_led.animation_running) {
    xSemaphoreTake(s_touch_led.mutex, portMAX_DELAY);

    switch (s_touch_led.current_animation) {
    case TOUCH_LED_ANIM_RAINBOW: {
      float hue = (s_touch_led.animation_step * 360.0f) / 255.0f;

      for (uint16_t i = 0; i < s_touch_led.config.led_count; i++) {
        float led_hue =
            fmodf(hue + (i * 360.0f / s_touch_led.config.led_count), 360.0f);
        rgb_color_t led_color = hsv_to_rgb(led_hue, 1.0f, 1.0f);
        apply_brightness(&led_color, s_touch_led.current_brightness);
        led_strip_set_pixel(s_touch_led.led_strip, i, led_color.red,
                            led_color.green, led_color.blue);
      }
      led_strip_refresh(s_touch_led.led_strip);
      break;
    }

    case TOUCH_LED_ANIM_BREATHE: {
      float brightness_factor =
          (sinf(s_touch_led.animation_step * 2.0f * M_PI / 255.0f) + 1.0f) /
          2.0f;
      rgb_color_t color = s_touch_led.animation_primary_color;
      uint8_t brightness =
          (uint8_t)(s_touch_led.current_brightness * brightness_factor);
      apply_brightness(&color, brightness);

      for (uint16_t i = 0; i < s_touch_led.config.led_count; i++) {
        led_strip_set_pixel(s_touch_led.led_strip, i, color.red, color.green,
                            color.blue);
      }
      led_strip_refresh(s_touch_led.led_strip);
      break;
    }

    case TOUCH_LED_ANIM_FADE: {
      rgb_color_t color = s_touch_led.animation_primary_color;
      uint8_t brightness = (s_touch_led.animation_step > 127)
                               ? (255 - s_touch_led.animation_step)
                               : s_touch_led.animation_step;
      brightness = (brightness * s_touch_led.current_brightness) / 127;
      apply_brightness(&color, brightness);

      for (uint16_t i = 0; i < s_touch_led.config.led_count; i++) {
        led_strip_set_pixel(s_touch_led.led_strip, i, color.red, color.green,
                            color.blue);
      }
      led_strip_refresh(s_touch_led.led_strip);
      break;
    }

    default:
      break;
    }

    s_touch_led.animation_step = (s_touch_led.animation_step + 1) % 256;
    xSemaphoreGive(s_touch_led.mutex);

    // Delay based on animation speed
    uint32_t delay_ms = 100 - (s_touch_led.animation_speed * 90 / 255);
    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(delay_ms));
  }

  vTaskDelete(NULL);
}

// Touch detection task implementation
static void touch_detection_task(void *arg) {
  bool last_touch_state = false;
  uint32_t press_start_time = 0;
  uint8_t debounce_counter = 0;
  static const uint8_t debounce_threshold = 3; // Require 3 consecutive readings

  while (s_touch_led.touch_enabled) {
    // Read GPIO level (for simple digital touch detection)
    int gpio_level = gpio_get_level(s_touch_led.config.touch_gpio);

    // Apply touch logic inversion if configured
    bool raw_touch =
        s_touch_led.config.touch_invert ? (gpio_level == 0) : (gpio_level == 1);

    // Simple debouncing
    if (raw_touch) {
      if (debounce_counter < debounce_threshold) {
        debounce_counter++;
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
    } else {
      debounce_counter = 0;
    }

    bool current_touch = (debounce_counter >= debounce_threshold);

    // Only process state changes to reduce mutex usage
    if (current_touch != last_touch_state) {
      // Update state with mutex protection
      if (xSemaphoreTake(s_touch_led.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_touch_led.is_touched = current_touch;
        touch_event_callback_t callback =
            s_touch_led.event_callback; // Store callback locally
        xSemaphoreGive(s_touch_led.mutex);

        // Call callback AFTER releasing mutex to avoid deadlock
        if (callback) {
          if (current_touch) {
            // Touch press
            press_start_time = esp_timer_get_time() / 1000;
            callback(TOUCH_EVENT_PRESS, 0);
          } else {
            // Touch release
            uint32_t duration =
                (esp_timer_get_time() / 1000) - press_start_time;
            if (duration > 1000) {
              callback(TOUCH_EVENT_LONG_PRESS, duration);
            } else {
              callback(TOUCH_EVENT_RELEASE, duration);
            }
          }
        }
      }

      last_touch_state = current_touch;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz sampling rate
  }

  vTaskDelete(NULL);
} // Console command implementations

// Helper function to parse color from string
static rgb_color_t parse_color(const char *color_str) {
  if (strcasecmp(color_str, "red") == 0)
    return TOUCH_LED_COLOR_RED;
  if (strcasecmp(color_str, "green") == 0)
    return TOUCH_LED_COLOR_GREEN;
  if (strcasecmp(color_str, "blue") == 0)
    return TOUCH_LED_COLOR_BLUE;
  if (strcasecmp(color_str, "white") == 0)
    return TOUCH_LED_COLOR_WHITE;
  if (strcasecmp(color_str, "yellow") == 0)
    return TOUCH_LED_COLOR_YELLOW;
  if (strcasecmp(color_str, "cyan") == 0)
    return TOUCH_LED_COLOR_CYAN;
  if (strcasecmp(color_str, "magenta") == 0)
    return TOUCH_LED_COLOR_MAGENTA;
  if (strcasecmp(color_str, "orange") == 0)
    return TOUCH_LED_COLOR_ORANGE;
  if (strcasecmp(color_str, "purple") == 0)
    return TOUCH_LED_COLOR_PURPLE;
  if (strcasecmp(color_str, "off") == 0)
    return TOUCH_LED_COLOR_OFF;

  // Try to parse as RGB hex format (e.g., "FF0000" for red)
  if (strlen(color_str) == 6) {
    unsigned int r, g, b;
    if (sscanf(color_str, "%02x%02x%02x", &r, &g, &b) == 3) {
      rgb_color_t color = {r, g, b};
      return color;
    }
  }

  // Default to off if parsing fails
  return TOUCH_LED_COLOR_OFF;
}

// Helper function to get color name string
static const char *get_color_name(rgb_color_t color) {
  if (memcmp(&color, &TOUCH_LED_COLOR_RED, sizeof(rgb_color_t)) == 0)
    return "Red";
  if (memcmp(&color, &TOUCH_LED_COLOR_GREEN, sizeof(rgb_color_t)) == 0)
    return "Green";
  if (memcmp(&color, &TOUCH_LED_COLOR_BLUE, sizeof(rgb_color_t)) == 0)
    return "Blue";
  if (memcmp(&color, &TOUCH_LED_COLOR_WHITE, sizeof(rgb_color_t)) == 0)
    return "White";
  if (memcmp(&color, &TOUCH_LED_COLOR_YELLOW, sizeof(rgb_color_t)) == 0)
    return "Yellow";
  if (memcmp(&color, &TOUCH_LED_COLOR_CYAN, sizeof(rgb_color_t)) == 0)
    return "Cyan";
  if (memcmp(&color, &TOUCH_LED_COLOR_MAGENTA, sizeof(rgb_color_t)) == 0)
    return "Magenta";
  if (memcmp(&color, &TOUCH_LED_COLOR_ORANGE, sizeof(rgb_color_t)) == 0)
    return "Orange";
  if (memcmp(&color, &TOUCH_LED_COLOR_PURPLE, sizeof(rgb_color_t)) == 0)
    return "Purple";
  if (memcmp(&color, &TOUCH_LED_COLOR_OFF, sizeof(rgb_color_t)) == 0)
    return "Off";
  return "Custom";
}

// Helper function to get animation name string
static const char *get_animation_name(touch_led_animation_t anim) {
  switch (anim) {
  case TOUCH_LED_ANIM_NONE:
    return "None";
  case TOUCH_LED_ANIM_FADE:
    return "Fade";
  case TOUCH_LED_ANIM_RAINBOW:
    return "Rainbow";
  case TOUCH_LED_ANIM_BREATHE:
    return "Breathe";
  case TOUCH_LED_ANIM_PULSE:
    return "Pulse";
  case TOUCH_LED_ANIM_WAVE:
    return "Wave";
  case TOUCH_LED_ANIM_SPARKLE:
    return "Sparkle";
  default:
    return "Unknown";
  }
}

// led status command
static esp_err_t cmd_led_status(int argc, char **argv) {
  if (!s_touch_led.is_initialized) {
    printf("Touch LED not initialized\n");
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t led_count;
  uint8_t brightness;
  touch_led_animation_t animation;

  esp_err_t ret = touch_led_get_status(&led_count, &brightness, &animation);
  if (ret != ESP_OK) {
    printf("Failed to get status: %s\n", esp_err_to_name(ret));
    return ret;
  }

  printf("Touch LED Status:\n");
  printf("================\n");
  printf("LED Count: %d\n", led_count);
  printf("Brightness: %d/255\n", brightness);
  printf("Animation: %s\n", get_animation_name(animation));
  printf("Touch Enabled: %s\n", s_touch_led.touch_enabled ? "Yes" : "No");
  printf("Touch State: %s\n",
         s_touch_led.is_touched ? "Touched" : "Not Touched");
  printf("Touch Value: %lu\n", touch_led_get_touch_value());
  printf("Touch Threshold: %lu\n", s_touch_led.config.touch_threshold);

  printf("\nHardware Configuration:\n");
  printf("LED GPIO: %d\n", s_touch_led.config.led_gpio);
  printf("Touch GPIO: %d\n", s_touch_led.config.touch_gpio);
  printf("Max Brightness: %lu\n", s_touch_led.config.max_brightness);

  return ESP_OK;
}

// led set command - set color for specific LED or all LEDs
static esp_err_t cmd_led_set(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: led set <color> [led_index]\n");
    printf(
        "  color: "
        "red|green|blue|white|yellow|cyan|magenta|orange|purple|off|RRGGBB\n");
    printf("  led_index: LED index (0-%d), omit for all LEDs\n",
           s_touch_led.config.led_count - 1);
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_touch_led.is_initialized) {
    printf("Touch LED not initialized\n");
    return ESP_ERR_INVALID_STATE;
  }

  // Stop any running animation to prevent color override
  touch_led_stop_animation();

  rgb_color_t color = parse_color(argv[1]);
  esp_err_t ret;

  if (argc >= 3) {
    // Set specific LED
    int led_index = atoi(argv[2]);
    if (led_index < 0 || led_index >= s_touch_led.config.led_count) {
      printf("Invalid LED index. Range: 0-%d\n",
             s_touch_led.config.led_count - 1);
      return ESP_ERR_INVALID_ARG;
    }

    ret = touch_led_set_color(led_index, color);
    if (ret == ESP_OK) {
      printf("Set LED %d to %s (%d,%d,%d)\n", led_index, get_color_name(color),
             color.red, color.green, color.blue);
    }
  } else {
    // Set all LEDs
    ret = touch_led_set_all_color(color);
    if (ret == ESP_OK) {
      printf("Set all LEDs to %s (%d,%d,%d)\n", get_color_name(color),
             color.red, color.green, color.blue);
    }
  }

  if (ret == ESP_OK) {
    ret = touch_led_update();
  }

  if (ret != ESP_OK) {
    printf("Failed to set LED color: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

// led brightness command
static esp_err_t cmd_led_brightness(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: led brightness <level>\n");
    printf("  level: brightness level (0-255)\n");
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_touch_led.is_initialized) {
    printf("Touch LED not initialized\n");
    return ESP_ERR_INVALID_STATE;
  }

  int brightness = atoi(argv[1]);
  if (brightness < 0 || brightness > 255) {
    printf("Invalid brightness level. Range: 0-255\n");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = touch_led_set_brightness((uint8_t)brightness);
  if (ret == ESP_OK) {
    ret = touch_led_update();
    printf("Set brightness to %d/255\n", brightness);
  } else {
    printf("Failed to set brightness: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

// led clear command
static esp_err_t cmd_led_clear(int argc, char **argv) {
  if (!s_touch_led.is_initialized) {
    printf("Touch LED not initialized\n");
    return ESP_ERR_INVALID_STATE;
  }

  // Stop any running animation to prevent override
  touch_led_stop_animation();

  esp_err_t ret = touch_led_clear();
  if (ret == ESP_OK) {
    ret = touch_led_update();
    printf("All LEDs cleared\n");
  } else {
    printf("Failed to clear LEDs: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

// led animation command
static esp_err_t cmd_led_animation(int argc, char **argv) {
  if (argc < 2) {
    printf(
        "Usage: led animation <start|stop> [mode] [speed] [color1] [color2]\n");
    printf("  start: start animation with specified parameters\n");
    printf("  stop: stop current animation\n");
    printf("  mode: none|fade|rainbow|breathe|pulse|wave|sparkle\n");
    printf("  speed: animation speed (1-255), higher is faster\n");
    printf("  color1: primary color "
           "(red|green|blue|white|yellow|cyan|magenta|orange|purple|off|RRGGBB)"
           "\n");
    printf("  color2: secondary color (optional, for some animations)\n");
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_touch_led.is_initialized) {
    printf("Touch LED not initialized\n");
    return ESP_ERR_INVALID_STATE;
  }

  if (strcasecmp(argv[1], "stop") == 0) {
    esp_err_t ret = touch_led_stop_animation();
    if (ret == ESP_OK) {
      printf("Animation stopped\n");
    } else {
      printf("Failed to stop animation: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  if (strcasecmp(argv[1], "start") == 0) {
    if (argc < 3) {
      printf("Animation mode required\n");
      return ESP_ERR_INVALID_ARG;
    }

    touch_led_animation_t animation = TOUCH_LED_ANIM_NONE;
    if (strcasecmp(argv[2], "fade") == 0)
      animation = TOUCH_LED_ANIM_FADE;
    else if (strcasecmp(argv[2], "rainbow") == 0)
      animation = TOUCH_LED_ANIM_RAINBOW;
    else if (strcasecmp(argv[2], "breathe") == 0)
      animation = TOUCH_LED_ANIM_BREATHE;
    else if (strcasecmp(argv[2], "pulse") == 0)
      animation = TOUCH_LED_ANIM_PULSE;
    else if (strcasecmp(argv[2], "wave") == 0)
      animation = TOUCH_LED_ANIM_WAVE;
    else if (strcasecmp(argv[2], "sparkle") == 0)
      animation = TOUCH_LED_ANIM_SPARKLE;
    else {
      printf("Invalid animation mode\n");
      return ESP_ERR_INVALID_ARG;
    }

    uint8_t speed = 100; // Default speed
    if (argc >= 4) {
      int s = atoi(argv[3]);
      if (s < 1 || s > 255) {
        printf("Invalid speed. Range: 1-255\n");
        return ESP_ERR_INVALID_ARG;
      }
      speed = (uint8_t)s;
    }

    rgb_color_t primary_color = TOUCH_LED_COLOR_RED; // Default primary color
    if (argc >= 5) {
      primary_color = parse_color(argv[4]);
    }

    rgb_color_t secondary_color =
        TOUCH_LED_COLOR_BLUE; // Default secondary color
    if (argc >= 6) {
      secondary_color = parse_color(argv[5]);
    }

    esp_err_t ret = touch_led_start_animation(animation, speed, primary_color,
                                              secondary_color);
    if (ret == ESP_OK) {
      printf("Started %s animation (speed: %d, primary: %s, secondary: %s)\n",
             get_animation_name(animation), speed,
             get_color_name(primary_color), get_color_name(secondary_color));
    } else {
      printf("Failed to start animation: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  printf("Invalid animation command\n");
  return ESP_ERR_INVALID_ARG;
}

// led help command
static int cmd_led_help(int argc, char **argv) {
  printf("Touch LED Control commands:\n");
  printf("==========================\n");
  printf("⚠️  Note: This controls the single WS2812 touch LED (1 LED)\n");
  printf("\n📊 Status and Information:\n");
  printf("  led touch status                  - Show LED status and "
         "configuration\n");
  printf("  led touch help                    - Show this help information\n");

  printf("\n🎨 LED Color Control:\n");
  printf("  led touch set <color>             - Set LED color (single LED "
         "only)\n");
  printf("    color: "
         "red|green|blue|white|yellow|cyan|magenta|orange|purple|off|RRGGBB\n");
  printf("  led touch brightness <level>      - Set brightness (0-255)\n");
  printf("  led touch clear                   - Turn off LED\n");

  printf("\n🌈 Animation Control:\n");
  printf("  led touch animation start <mode> [speed] [color1] [color2]\n");
  printf("    mode: fade|rainbow|breathe|pulse|wave|sparkle\n");
  printf("    speed: 1-255 (higher is faster), default 100\n");
  printf("    color1: primary color, default red\n");
  printf("    color2: secondary color, default blue\n");
  printf("  led touch animation stop          - Stop current animation\n");

  printf("\n👆 Touch Sensor Control:\n");
  printf("  led touch sensor enable           - Enable touch detection\n");
  printf("  led touch sensor disable          - Disable touch detection\n");
  printf(
      "  led touch sensor threshold <val>  - Set touch threshold (0-4095)\n");

  printf("\n� Configuration Management:\n");
  printf("  led touch config save             - Save current settings to "
         "memory\n");
  printf("  led touch config load             - Load saved settings\n");
  printf("  led touch config reset            - Reset to factory defaults\n");

  printf("\n�💡 Usage Examples:\n");
  printf("  led touch status                  - Check LED and touch status\n");
  printf("  led touch set red                 - Set LED to red\n");
  printf("  led touch set FF6600              - Set LED to orange (RGB hex)\n");
  printf("  led touch brightness 128          - Set brightness to 50%%\n");
  printf("  led touch animation start rainbow 150 - Fast rainbow animation\n");
  printf(
      "  led touch animation start breathe 50 green - Slow green breathing\n");
  printf("  led touch sensor threshold 800    - Set touch sensitivity\n");

  printf("\n🎨 Available Colors:\n");
  printf("  red, green, blue, white, yellow, cyan, magenta, orange, purple, "
         "off\n");
  printf("  Or use RGB hex format: RRGGBB (e.g., FF0000 for red)\n");

  printf("\n🌈 Animation Modes:\n");
  printf("  fade     - Fade in/out effect\n");
  printf("  rainbow  - Cycling rainbow colors\n");
  printf("  breathe  - Breathing effect with specified color\n");
  printf("  pulse    - Quick pulse effect\n");
  printf("  wave     - Wave propagation effect\n");
  printf("  sparkle  - Random sparkle effect\n");

  return ESP_OK;
}

// Touch sensor control commands (now separated from LED control)
static esp_err_t cmd_led_touch_sensor(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: led touch sensor <enable|disable|threshold> [value]\n");
    printf("  enable: enable touch detection\n");
    printf("  disable: disable touch detection\n");
    printf("  threshold <value>: set touch threshold (0-4095)\n");
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_touch_led.is_initialized) {
    printf("Touch LED not initialized\n");
    return ESP_ERR_INVALID_STATE;
  }

  if (strcasecmp(argv[1], "enable") == 0) {
    esp_err_t ret = touch_led_set_touch_enable(true);
    if (ret == ESP_OK) {
      printf("Touch detection enabled\n");
    } else {
      printf("Failed to enable touch detection: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  if (strcasecmp(argv[1], "disable") == 0) {
    esp_err_t ret = touch_led_set_touch_enable(false);
    if (ret == ESP_OK) {
      printf("Touch detection disabled\n");
    } else {
      printf("Failed to disable touch detection: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  if (strcasecmp(argv[1], "threshold") == 0) {
    if (argc != 3) {
      printf("Threshold value required\n");
      return ESP_ERR_INVALID_ARG;
    }

    int threshold = atoi(argv[2]);
    if (threshold < 0 || threshold > 4095) {
      printf("Invalid threshold. Range: 0-4095\n");
      return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = touch_led_set_touch_threshold((uint32_t)threshold);
    if (ret == ESP_OK) {
      printf("Touch threshold set to %d\n", threshold);
    } else {
      printf("Failed to set touch threshold: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  printf("Invalid touch sensor command\n");
  return ESP_ERR_INVALID_ARG;
}

// Configuration management commands
static esp_err_t cmd_led_config(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: led touch config <save|load|reset>\n");
    return ESP_ERR_INVALID_ARG;
  }

  if (strcasecmp(argv[1], "save") == 0) {
    esp_err_t ret = touch_led_save_config();
    if (ret == ESP_OK) {
      printf("Touch LED configuration saved to memory\n");
    } else {
      printf("Failed to save configuration: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  if (strcasecmp(argv[1], "load") == 0) {
    esp_err_t ret = touch_led_load_config();
    if (ret == ESP_OK) {
      printf("Touch LED configuration loaded from memory\n");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      printf("No saved configuration found, using current settings\n");
      ret = ESP_OK;
    } else {
      printf("Failed to load configuration: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  if (strcasecmp(argv[1], "reset") == 0) {
    esp_err_t ret = touch_led_reset_config();
    if (ret == ESP_OK) {
      printf("Touch LED configuration reset to factory defaults\n");
    } else {
      printf("Failed to reset configuration: %s\n", esp_err_to_name(ret));
    }
    return ret;
  }

  printf("Invalid config command. Use: save|load|reset\n");
  return ESP_ERR_INVALID_ARG;
}

// Main led command dispatcher - handles "led touch" commands
static esp_err_t cmd_led_touch(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: led touch <command>\n");
    printf("Available commands: "
           "status|set|brightness|clear|animation|sensor|config|help\n");
    printf("Use 'led touch help' for detailed information\n");
    return ESP_ERR_INVALID_ARG;
  }

  // Check if first argument is "touch"
  if (strcasecmp(argv[1], "touch") == 0) {
    if (argc < 3) {
      return cmd_led_status(argc, argv);
    }

    if (strcasecmp(argv[2], "status") == 0) {
      return cmd_led_status(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "set") == 0) {
      return cmd_led_set(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "brightness") == 0) {
      return cmd_led_brightness(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "clear") == 0) {
      return cmd_led_clear(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "animation") == 0) {
      return cmd_led_animation(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "sensor") == 0) {
      return cmd_led_touch_sensor(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "config") == 0) {
      return cmd_led_config(argc - 2, argv + 2);
    }
    if (strcasecmp(argv[2], "help") == 0) {
      return cmd_led_help(argc - 2, argv + 2);
    }

    printf("Unknown touch LED command: %s\n", argv[2]);
    printf("Use 'led touch help' for available commands\n");
    return ESP_ERR_INVALID_ARG;
  } else if (strcasecmp(argv[1], "board") == 0) {
    // Forward board LED commands to board_led component
    if (board_led_is_initialized()) {
      // Call board_led's console handler with the remaining arguments
      return board_led_console_handler(argc, argv);
    } else {
      printf("Board LED system not initialized\n");
      return ESP_ERR_INVALID_STATE;
    }
  } else if (strcasecmp(argv[1], "matrix") == 0) {
    // Forward matrix LED commands to matrix_led component
    if (matrix_led_is_initialized()) {
      // Call matrix_led's console handler with the remaining arguments
      return matrix_led_cmd_handler(argc - 1, argv + 1);
    } else {
      printf("Matrix LED system not initialized\n");
      return ESP_ERR_INVALID_STATE;
    }
  } else {
    printf("LED subsystem not recognized: %s\n", argv[1]);
    printf("Available subsystems:\n");
    printf("  led touch <command>  - Control touch LED\n");
    if (board_led_is_initialized()) {
      printf("  led board <command>  - Control board LEDs\n");
    }
    if (matrix_led_is_initialized()) {
      printf("  led matrix <command> - Control 32x32 LED matrix\n");
    }
    printf("Use 'led <subsystem> help' for available commands\n");
    return ESP_ERR_INVALID_ARG;
  }
}

esp_err_t touch_led_register_commands(void) {
  const console_cmd_t led_touch_cmd = {
      .command = "led",
      .help = "LED control commands (use 'led touch' for touch LED)",
      .hint = "touch",
      .func = &cmd_led_touch,
      .min_args = 1,
      .max_args = 0, // unlimited
  };

  esp_err_t ret = console_register_command(&led_touch_cmd);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Touch LED commands registered under 'led touch'");
  } else {
    ESP_LOGE(TAG, "Failed to register LED commands: %s", esp_err_to_name(ret));
  }

  return ret;
}

/* ============================================================================
 * Configuration Management Functions
 * ============================================================================
 */

esp_err_t touch_led_save_config(void) {
  if (!s_touch_led.is_initialized) {
    ESP_LOGE(TAG, "Touch LED not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Initialize config manager if not already done
  if (!config_manager_is_initialized()) {
    config_manager_config_t config_mgr_config =
        config_manager_get_default_config();
    esp_err_t config_ret = config_manager_init(&config_mgr_config);
    if (config_ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize config manager: %s",
               esp_err_to_name(config_ret));
      return config_ret;
    }
  }

  // Prepare configuration data
  touch_led_saved_config_t saved_config = {
      .version = TOUCH_LED_CONFIG_VERSION,
      .is_enabled = s_touch_led.is_initialized,
      .brightness = s_touch_led.current_brightness,
      .static_color = s_touch_led.has_static_color
                          ? s_touch_led.current_static_color
                          : (rgb_color_t){0, 0, 0},
      .has_static_color = s_touch_led.has_static_color,
      .animation = s_touch_led.current_animation,
      .animation_speed = s_touch_led.animation_speed,
      .animation_primary = s_touch_led.animation_primary_color,
      .animation_secondary = s_touch_led.animation_secondary_color,
      .animation_running = s_touch_led.animation_running,
      .touch_enabled = s_touch_led.touch_enabled,
      .touch_threshold = s_touch_led.config.touch_threshold};

  // Save configuration to NVS
  esp_err_t ret =
      config_manager_set(TOUCH_LED_CONFIG_NAMESPACE, TOUCH_LED_CONFIG_KEY,
                         CONFIG_TYPE_BLOB, &saved_config, sizeof(saved_config));

  if (ret == ESP_OK) {
    ESP_LOGI(TAG,
             "Touch LED configuration saved (brightness: %d, animation: %d, "
             "running: %s)",
             saved_config.brightness, saved_config.animation,
             saved_config.animation_running ? "yes" : "no");
    config_manager_commit();
  } else {
    ESP_LOGE(TAG, "Failed to save touch LED config: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t touch_led_load_config(void) {
  if (!s_touch_led.is_initialized) {
    ESP_LOGE(TAG, "Touch LED not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Initialize config manager if not already done
  if (!config_manager_is_initialized()) {
    config_manager_config_t config_mgr_config =
        config_manager_get_default_config();
    esp_err_t config_ret = config_manager_init(&config_mgr_config);
    if (config_ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize config manager: %s",
               esp_err_to_name(config_ret));
      return config_ret;
    }
  }

  touch_led_saved_config_t saved_config;
  size_t config_size = sizeof(saved_config);

  esp_err_t ret =
      config_manager_get(TOUCH_LED_CONFIG_NAMESPACE, TOUCH_LED_CONFIG_KEY,
                         CONFIG_TYPE_BLOB, &saved_config, &config_size);

  if (ret == ESP_OK) {
    // Check version compatibility
    if (saved_config.version != TOUCH_LED_CONFIG_VERSION) {
      ESP_LOGW(TAG,
               "Configuration version mismatch (saved: %d, current: %d), using "
               "defaults",
               saved_config.version, TOUCH_LED_CONFIG_VERSION);
      return ESP_ERR_NOT_SUPPORTED;
    }

    // Apply loaded configuration
    ESP_LOGI(TAG, "Loading touch LED configuration...");

    // Set brightness
    s_touch_led.current_brightness = saved_config.brightness;

    // Set touch settings
    s_touch_led.touch_enabled = saved_config.touch_enabled;
    s_touch_led.config.touch_threshold = saved_config.touch_threshold;

    // Restore animation state
    if (saved_config.animation_running &&
        saved_config.animation != TOUCH_LED_ANIM_NONE) {
      s_touch_led.animation_primary_color = saved_config.animation_primary;
      s_touch_led.animation_secondary_color = saved_config.animation_secondary;
      s_touch_led.animation_speed = saved_config.animation_speed;

      // Start the saved animation
      esp_err_t anim_ret = touch_led_start_animation(
          saved_config.animation, saved_config.animation_speed,
          saved_config.animation_primary, saved_config.animation_secondary);
      if (anim_ret == ESP_OK) {
        ESP_LOGI(TAG, "Restored animation: %d at speed %d",
                 saved_config.animation, saved_config.animation_speed);
      }
    } else if (saved_config.has_static_color) {
      // Restore static color
      esp_err_t color_ret = touch_led_set_all_color(saved_config.static_color);
      if (color_ret == ESP_OK) {
        // Update LED display to show the static color
        touch_led_update();
        ESP_LOGI(TAG, "Restored static color: RGB(%d,%d,%d)",
                 saved_config.static_color.red, saved_config.static_color.green,
                 saved_config.static_color.blue);
      }
    }

    ESP_LOGI(TAG, "Touch LED configuration loaded successfully");

  } else if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGI(TAG, "No saved configuration found, setting up default state");

    // Set default LED state - soft blue glow with breathing animation
    touch_led_set_all_color(TOUCH_LED_COLOR_BLUE);
    touch_led_set_brightness(50);
    touch_led_update();

    // Start a gentle breathing animation as default
    touch_led_start_animation(TOUCH_LED_ANIM_BREATHE, 30, TOUCH_LED_COLOR_BLUE,
                              TOUCH_LED_COLOR_OFF);

    ESP_LOGI(TAG, "Default LED state initialized");
    ret = ESP_OK; // Not an error
  } else {
    ESP_LOGE(TAG, "Failed to load touch LED config: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t touch_led_reset_config(void) {
  // Initialize config manager if not already done
  if (!config_manager_is_initialized()) {
    config_manager_config_t config_mgr_config =
        config_manager_get_default_config();
    esp_err_t config_ret = config_manager_init(&config_mgr_config);
    if (config_ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize config manager: %s",
               esp_err_to_name(config_ret));
      return config_ret;
    }
  }

  // Delete the configuration from NVS
  esp_err_t ret =
      config_manager_delete(TOUCH_LED_CONFIG_NAMESPACE, TOUCH_LED_CONFIG_KEY);

  if (ret == ESP_OK || ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGI(TAG, "Touch LED configuration reset to defaults");
    config_manager_commit();

    // Reset to default state if initialized
    if (s_touch_led.is_initialized) {
      // Stop any running animations
      touch_led_stop_animation();

      // Clear all LEDs
      touch_led_clear();

      // Reset to default settings
      s_touch_led.current_brightness = 100; // Default brightness
      s_touch_led.touch_enabled = true;     // Enable touch by default

      ESP_LOGI(TAG, "Touch LED reset to factory defaults");
    }

    ret = ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to reset touch LED config: %s", esp_err_to_name(ret));
  }

  return ret;
}