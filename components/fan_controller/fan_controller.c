/**
 * @file fan_controller.c
 * @brief Fan Controller Component Implementation
 *
 * @author robOS Team
 * @date 2025
 */

#include "fan_controller.h"
#include "config_manager.h"
#include "console_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Constants and Private Macros
 * ============================================================================
 */

static const char *TAG = "FAN_CONTROLLER";

#define FAN_CONTROLLER_TASK_STACK_SIZE 4096
#define FAN_CONTROLLER_TASK_PRIORITY 5
#define FAN_CONTROLLER_DEFAULT_UPDATE_INTERVAL 1000
#define MAX_FANS 4
#define DEFAULT_PWM_FREQUENCY 25000 // 25kHz
#define DEFAULT_PWM_RESOLUTION 10   // 10-bit resolution (0-1023)
#define FAN_CONFIG_NAMESPACE "fan_config"

/* ============================================================================
 * Private Type Definitions
 * ============================================================================
 */

typedef struct {
  fan_config_t config;
  fan_status_t status;
  fan_curve_point_t *curve_points;
  uint8_t num_curve_points;
  bool curve_enabled;
} fan_instance_t;

typedef struct {
  bool initialized;
  uint8_t num_fans;
  fan_instance_t fans[FAN_CONTROLLER_MAX_FANS];
  TaskHandle_t task_handle;
  SemaphoreHandle_t mutex;
  uint32_t update_interval_ms;
  bool enable_tachometer;
} fan_controller_context_t;

/* ============================================================================
 * Private Variables
 * ============================================================================
 */

static fan_controller_context_t s_fan_ctx = {0};

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

static void fan_controller_task(void *pvParameters);
static esp_err_t fan_controller_update_pwm(uint8_t fan_id,
                                           uint8_t speed_percent);
static esp_err_t fan_controller_apply_curve(uint8_t fan_id, float temperature);
static uint8_t fan_controller_interpolate_speed(const fan_curve_point_t *curve,
                                                uint8_t num_points,
                                                float temperature);

// Console command functions
static esp_err_t cmd_fan_status(int argc, char **argv);
static esp_err_t cmd_fan_set(int argc, char **argv);
static esp_err_t cmd_fan_mode(int argc, char **argv);
static esp_err_t cmd_fan_enable(int argc, char **argv);
static esp_err_t cmd_fan_gpio(int argc, char **argv);
static esp_err_t cmd_fan_config(int argc, char **argv);
static esp_err_t cmd_fan_help(void);
static esp_err_t save_fan_config(uint8_t fan_id);
static esp_err_t save_fan_full_config(uint8_t fan_id);
static esp_err_t load_fan_config(uint8_t fan_id);
static esp_err_t load_fan_full_config(uint8_t fan_id);
static esp_err_t load_all_fan_configs(void);

// Full configuration structure for saving runtime state
typedef struct {
  fan_config_t hardware_config;       // Hardware configuration
  fan_mode_t current_mode;            // Current operating mode
  uint8_t current_speed;              // Current speed setting
  bool enabled;                       // Current enabled state
  uint8_t num_curve_points;           // Number of curve points
  fan_curve_point_t curve_points[10]; // Temperature curve points (max 10)
  bool curve_enabled;                 // Whether curve is enabled
  uint32_t version;                   // Configuration version for compatibility
} fan_full_config_t;

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

fan_controller_config_t fan_controller_get_default_config(void) {
  fan_controller_config_t config = {.num_fans = 1,
                                    .fan_configs = NULL,
                                    .enable_tachometer = false,
                                    .update_interval_ms =
                                        FAN_CONTROLLER_DEFAULT_UPDATE_INTERVAL};
  return config;
}

esp_err_t fan_controller_init(const fan_controller_config_t *config) {
  if (s_fan_ctx.initialized) {
    ESP_LOGW(TAG, "Fan controller already initialized");
    return ESP_OK;
  }

  // Use default config if NULL
  fan_controller_config_t default_config = fan_controller_get_default_config();
  if (config == NULL) {
    config = &default_config;
  }

  ESP_LOGI(TAG, "Initializing fan controller...");

  // Validate configuration
  if (config->num_fans == 0 || config->num_fans > FAN_CONTROLLER_MAX_FANS) {
    ESP_LOGE(TAG, "Invalid number of fans: %d", config->num_fans);
    return ESP_ERR_INVALID_ARG;
  }

  // Create mutex
  s_fan_ctx.mutex = xSemaphoreCreateMutex();
  if (s_fan_ctx.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }

  // Initialize context
  s_fan_ctx.num_fans = config->num_fans;
  s_fan_ctx.enable_tachometer = config->enable_tachometer;
  s_fan_ctx.update_interval_ms = config->update_interval_ms;

  // Initialize configuration manager if not already done
  if (!config_manager_is_initialized()) {
    config_manager_config_t config_mgr_config =
        config_manager_get_default_config();
    esp_err_t config_ret = config_manager_init(&config_mgr_config);
    if (config_ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize config manager: %s",
               esp_err_to_name(config_ret));
      vSemaphoreDelete(s_fan_ctx.mutex);
      return config_ret;
    }
  }

  // Initialize fans with default configuration (will be overridden by loaded
  // config if available)
  for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
    fan_instance_t *fan = &s_fan_ctx.fans[i];

    if (config->fan_configs != NULL) {
      fan->config = config->fan_configs[i];
    } else {
      // Use default configuration
      fan->config.fan_id = i;
      fan->config.pwm_pin = (i == 0) ? 41 : -1; // Fan 0 PWM connected to GPIO41
      fan->config.tach_pin = -1;
      fan->config.pwm_channel = (ledc_channel_t)i;
      fan->config.pwm_timer = LEDC_TIMER_0;
      fan->config.default_mode = FAN_MODE_MANUAL;
      fan->config.default_speed = 50;
      fan->config.invert_pwm = false;
    }

    // Initialize fan status
    fan->status.fan_id = i;
    fan->status.enabled = false; // Will be set properly after loading config
    fan->status.mode = fan->config.default_mode;
    fan->status.speed_percent = fan->config.default_speed;
    fan->status.rpm = 0;
    fan->status.temperature = 25.0f;
    fan->status.fault = false;

    // Initialize curve
    fan->curve_points = NULL;
    fan->num_curve_points = 0;
    fan->curve_enabled = false;

    // Do NOT configure PWM here - it will be done after loading saved config
  }

  // Create fan controller task
  BaseType_t ret =
      xTaskCreate(fan_controller_task, "fan_controller",
                  FAN_CONTROLLER_TASK_STACK_SIZE / sizeof(StackType_t), NULL,
                  FAN_CONTROLLER_TASK_PRIORITY, &s_fan_ctx.task_handle);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create fan controller task");
    vSemaphoreDelete(s_fan_ctx.mutex);
    return ESP_ERR_NO_MEM;
  }

  // Load saved configurations from NVS
  load_all_fan_configs();

  s_fan_ctx.initialized = true;
  ESP_LOGI(TAG, "Fan controller initialized successfully with %d fans",
           s_fan_ctx.num_fans);

  return ESP_OK;
}

esp_err_t fan_controller_deinit(void) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  // Delete task
  if (s_fan_ctx.task_handle != NULL) {
    vTaskDelete(s_fan_ctx.task_handle);
    s_fan_ctx.task_handle = NULL;
  }

  // Clean up fan configurations
  for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
    fan_instance_t *fan = &s_fan_ctx.fans[i];

    // Stop fan
    if (fan->config.pwm_pin >= 0) {
      hal_pwm_set_duty(fan->config.pwm_channel, 0);
    }

    // Free curve points
    if (fan->curve_points != NULL) {
      free(fan->curve_points);
      fan->curve_points = NULL;
    }
  }

  // Delete mutex
  if (s_fan_ctx.mutex != NULL) {
    vSemaphoreDelete(s_fan_ctx.mutex);
    s_fan_ctx.mutex = NULL;
  }

  memset(&s_fan_ctx, 0, sizeof(s_fan_ctx));
  ESP_LOGI(TAG, "Fan controller deinitialized");

  return ESP_OK;
}

bool fan_controller_is_initialized(void) { return s_fan_ctx.initialized; }

esp_err_t fan_controller_set_speed(uint8_t fan_id, uint8_t speed_percent) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (speed_percent > FAN_CONTROLLER_MAX_SPEED) {
    speed_percent = FAN_CONTROLLER_MAX_SPEED;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  fan_instance_t *fan = &s_fan_ctx.fans[fan_id];
  fan->status.speed_percent = speed_percent;
  fan->status.mode = FAN_MODE_MANUAL;

  esp_err_t ret = fan_controller_update_pwm(fan_id, speed_percent);

  xSemaphoreGive(s_fan_ctx.mutex);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Fan %d speed set to %d%%", fan_id, speed_percent);
  }

  return ret;
}

esp_err_t fan_controller_get_speed(uint8_t fan_id, uint8_t *speed_percent) {
  if (!s_fan_ctx.initialized || speed_percent == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  *speed_percent = s_fan_ctx.fans[fan_id].status.speed_percent;

  xSemaphoreGive(s_fan_ctx.mutex);
  return ESP_OK;
}

esp_err_t fan_controller_set_mode(uint8_t fan_id, fan_mode_t mode) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  s_fan_ctx.fans[fan_id].status.mode = mode;

  xSemaphoreGive(s_fan_ctx.mutex);

  ESP_LOGI(TAG, "Fan %d mode set to %d", fan_id, mode);
  return ESP_OK;
}

esp_err_t fan_controller_get_mode(uint8_t fan_id, fan_mode_t *mode) {
  if (!s_fan_ctx.initialized || mode == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  *mode = s_fan_ctx.fans[fan_id].status.mode;

  xSemaphoreGive(s_fan_ctx.mutex);
  return ESP_OK;
}

esp_err_t fan_controller_enable(uint8_t fan_id, bool enable) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  fan_instance_t *fan = &s_fan_ctx.fans[fan_id];
  fan->status.enabled = enable;

  esp_err_t ret = ESP_OK;
  if (!enable) {
    // Stop fan when disabled
    ret = fan_controller_update_pwm(fan_id, 0);
  } else {
    // Resume fan at current speed when enabled
    ret = fan_controller_update_pwm(fan_id, fan->status.speed_percent);
  }

  xSemaphoreGive(s_fan_ctx.mutex);

  ESP_LOGI(TAG, "Fan %d %s", fan_id, enable ? "enabled" : "disabled");
  return ret;
}

esp_err_t fan_controller_is_enabled(uint8_t fan_id, bool *enabled) {
  if (!s_fan_ctx.initialized || enabled == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  *enabled = s_fan_ctx.fans[fan_id].status.enabled;

  xSemaphoreGive(s_fan_ctx.mutex);
  return ESP_OK;
}

esp_err_t fan_controller_get_status(uint8_t fan_id, fan_status_t *status) {
  if (!s_fan_ctx.initialized || status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  *status = s_fan_ctx.fans[fan_id].status;

  xSemaphoreGive(s_fan_ctx.mutex);
  return ESP_OK;
}

esp_err_t fan_controller_get_all_status(fan_status_t *status_array,
                                        uint8_t array_size) {
  if (!s_fan_ctx.initialized || status_array == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (array_size < s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_SIZE;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
    status_array[i] = s_fan_ctx.fans[i].status;
  }

  xSemaphoreGive(s_fan_ctx.mutex);
  return ESP_OK;
}

esp_err_t fan_controller_configure_gpio(uint8_t fan_id, int pwm_pin,
                                        int pwm_channel) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (pwm_pin < 0 || pwm_pin > 48) { // ESP32S3 GPIO range
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  fan_instance_t *fan = &s_fan_ctx.fans[fan_id];

  // Stop current PWM if active
  if (fan->config.pwm_pin >= 0 && fan->status.enabled) {
    hal_pwm_set_duty(fan->config.pwm_channel, 0);
    ESP_LOGI(TAG, "Stopped PWM on old pin %d", fan->config.pwm_pin);
  }

  // Update configuration
  fan->config.pwm_pin = pwm_pin;
  if (pwm_channel >= 0) {
    fan->config.pwm_channel = (ledc_channel_t)pwm_channel;
  }

  // Configure new PWM
  hal_pwm_config_t pwm_config = {
      .channel = fan->config.pwm_channel,
      .pin = (gpio_num_t)fan->config.pwm_pin,
      .timer = fan->config.pwm_timer,
      .frequency = FAN_CONTROLLER_PWM_FREQUENCY,
      .resolution = FAN_CONTROLLER_PWM_RESOLUTION,
      .duty_cycle = (fan->config.default_speed *
                     ((1 << FAN_CONTROLLER_PWM_RESOLUTION) - 1)) /
                    100,
      .invert = fan->config.invert_pwm};

  esp_err_t ret = hal_pwm_configure(&pwm_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure PWM on new pin %d: %s", pwm_pin,
             esp_err_to_name(ret));
    fan->status.enabled = false;
    fan->status.fault = true;
  } else {
    ESP_LOGI(TAG, "Fan %d reconfigured: GPIO%d, Channel %d", fan_id, pwm_pin,
             fan->config.pwm_channel);
    fan->status.enabled = true;
    fan->status.fault = false;

    // Apply current speed to new pin
    fan_controller_update_pwm(fan_id, fan->status.speed_percent);

    // Save configuration to NVS
    save_fan_config(fan_id);
  }

  xSemaphoreGive(s_fan_ctx.mutex);
  return ret;
}

esp_err_t fan_controller_set_curve(uint8_t fan_id,
                                   const fan_curve_point_t *curve_points,
                                   uint8_t num_points) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  if (curve_points == NULL || num_points == 0 || num_points > 10) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_fan_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  fan_instance_t *fan = &s_fan_ctx.fans[fan_id];

  // Free existing curve points
  if (fan->curve_points != NULL) {
    free(fan->curve_points);
    fan->curve_points = NULL;
    fan->num_curve_points = 0;
  }

  // Allocate new curve points
  fan->curve_points =
      (fan_curve_point_t *)malloc(sizeof(fan_curve_point_t) * num_points);
  if (fan->curve_points == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for curve points");
    xSemaphoreGive(s_fan_ctx.mutex);
    return ESP_ERR_NO_MEM;
  }

  // Copy curve points and sort by temperature
  memcpy(fan->curve_points, curve_points,
         sizeof(fan_curve_point_t) * num_points);
  fan->num_curve_points = num_points;
  fan->curve_enabled = true;

  // Sort curve points by temperature (simple bubble sort)
  for (uint8_t i = 0; i < num_points - 1; i++) {
    for (uint8_t j = 0; j < num_points - i - 1; j++) {
      if (fan->curve_points[j].temperature >
          fan->curve_points[j + 1].temperature) {
        fan_curve_point_t temp = fan->curve_points[j];
        fan->curve_points[j] = fan->curve_points[j + 1];
        fan->curve_points[j + 1] = temp;
      }
    }
  }

  ESP_LOGI(TAG, "Fan %d curve configured with %d points", fan_id, num_points);
  xSemaphoreGive(s_fan_ctx.mutex);

  // Automatically save the configuration with curve data
  esp_err_t save_ret = save_fan_full_config(fan_id);
  if (save_ret == ESP_OK) {
    ESP_LOGI(TAG, "Fan %d curve configuration saved to NVS", fan_id);
  } else {
    ESP_LOGW(TAG, "Failed to save fan %d curve configuration: %s", fan_id,
             esp_err_to_name(save_ret));
  }

  return ESP_OK;
}

esp_err_t fan_controller_register_commands(void) {
  if (!s_fan_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  // Register fan commands
  console_cmd_t fan_commands[] = {
      {.command = "fan",
       .help = "fan <command> [args...] - PWM fan controller (type 'fan help' "
               "for details)",
       .hint = "<status|set|mode|enable|gpio|config|help> [args...]",
       .func = cmd_fan_status,
       .min_args = 0,
       .max_args = 15}};

  for (size_t i = 0; i < sizeof(fan_commands) / sizeof(fan_commands[0]); i++) {
    esp_err_t ret = console_register_command(&fan_commands[i]);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register command '%s': %s",
               fan_commands[i].command, esp_err_to_name(ret));
      return ret;
    }
  }

  ESP_LOGI(TAG, "Fan controller commands registered");
  return ESP_OK;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static float get_fan_temperature_for_mode(uint8_t fan_id) {
  // Use smart temperature source selection with priority system
  float temperature = 25.0f;
  temp_source_type_t source;

  if (console_get_effective_temperature(&temperature, &source) == ESP_OK) {
    return temperature;
  }

  // Fallback: use current status temperature if fan is valid
  if (fan_id < s_fan_ctx.num_fans) {
    return s_fan_ctx.fans[fan_id].status.temperature;
  }

  // Final fallback
  return 25.0f;
}

static void fan_controller_task(void *pvParameters) {
  ESP_LOGI(TAG, "Fan controller task started");

  // Wait for system to stabilize before starting PWM operations
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(TAG, "Fan controller task ready, starting PWM operations");

  while (1) {
    for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
      fan_instance_t *fan = &s_fan_ctx.fans[i];
      if (fan->status.enabled) {
        switch (fan->status.mode) {
        case FAN_MODE_MANUAL:
          fan_controller_update_pwm(i, fan->status.speed_percent);
          break;
        case FAN_MODE_AUTO_TEMP:
          // TODO: Use real sensor value
          fan_controller_apply_curve(i, fan->status.temperature);
          break;
        case FAN_MODE_AUTO_CURVE:
          // Use test temperature for debugging
          fan_controller_apply_curve(i, get_fan_temperature_for_mode(i));
          break;
        case FAN_MODE_OFF:
        default:
          fan_controller_update_pwm(i, 0);
          break;
        }
      } else {
        fan_controller_update_pwm(i, 0);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(s_fan_ctx.update_interval_ms));
  }

  ESP_LOGI(TAG, "Fan controller task ended");
  vTaskDelete(NULL);
}

static esp_err_t fan_controller_update_pwm(uint8_t fan_id,
                                           uint8_t speed_percent) {
  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  fan_instance_t *fan = &s_fan_ctx.fans[fan_id];

  if (fan->config.pwm_pin < 0) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (!fan->status.enabled) {
    speed_percent = 0;
  }

  if (speed_percent > FAN_CONTROLLER_MAX_SPEED) {
    speed_percent = FAN_CONTROLLER_MAX_SPEED;
  }

  // Apply PWM inversion if configured
  uint8_t actual_speed =
      fan->config.invert_pwm ? (100 - speed_percent) : speed_percent;

  // Convert percentage to PWM duty cycle value
  uint32_t max_duty = (1 << FAN_CONTROLLER_PWM_RESOLUTION) - 1;
  uint32_t duty_cycle = (actual_speed * max_duty) / 100;

  esp_err_t ret = hal_pwm_set_duty(fan->config.pwm_channel, duty_cycle);
  if (ret == ESP_OK) {
    fan->status.speed_percent = speed_percent;
  } else if (ret == ESP_ERR_INVALID_STATE) {
    // LEDC not initialized, try to reinitialize PWM
    ESP_LOGW(TAG,
             "LEDC not initialized for fan %d, attempting to reinitialize PWM",
             fan_id);

    hal_pwm_config_t pwm_config = {.channel = fan->config.pwm_channel,
                                   .pin = (gpio_num_t)fan->config.pwm_pin,
                                   .timer = fan->config.pwm_timer,
                                   .frequency = FAN_CONTROLLER_PWM_FREQUENCY,
                                   .resolution = FAN_CONTROLLER_PWM_RESOLUTION,
                                   .duty_cycle = duty_cycle,
                                   .invert = fan->config.invert_pwm};

    esp_err_t init_ret = hal_pwm_configure(&pwm_config);
    if (init_ret == ESP_OK) {
      ESP_LOGI(TAG, "Fan %d PWM reinitialized successfully", fan_id);
      fan->status.speed_percent = speed_percent;
      fan->status.fault = false;
      ret = ESP_OK;
    } else {
      ESP_LOGE(TAG, "Failed to reinitialize PWM for fan %d: %s", fan_id,
               esp_err_to_name(init_ret));
      fan->status.fault = true;
      ret = init_ret;
    }
  } else {
    ESP_LOGE(TAG, "Failed to set PWM duty for fan %d: %s", fan_id,
             esp_err_to_name(ret));
    fan->status.fault = true;
  }

  return ret;
}

static esp_err_t fan_controller_apply_curve(uint8_t fan_id, float temperature) {
  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  fan_instance_t *fan = &s_fan_ctx.fans[fan_id];

  if (fan->curve_points == NULL || fan->num_curve_points == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t speed = fan_controller_interpolate_speed(
      fan->curve_points, fan->num_curve_points, temperature);

  return fan_controller_update_pwm(fan_id, speed);
}

static uint8_t fan_controller_interpolate_speed(const fan_curve_point_t *curve,
                                                uint8_t num_points,
                                                float temperature) {
  if (curve == NULL || num_points == 0) {
    return 0;
  }

  if (num_points == 1) {
    return curve[0].speed_percent;
  }

  // Find the appropriate range for interpolation
  if (temperature <= curve[0].temperature) {
    return curve[0].speed_percent;
  }

  if (temperature >= curve[num_points - 1].temperature) {
    return curve[num_points - 1].speed_percent;
  }

  // Linear interpolation between two points
  for (uint8_t i = 0; i < num_points - 1; i++) {
    if (temperature >= curve[i].temperature &&
        temperature <= curve[i + 1].temperature) {
      float temp_range = curve[i + 1].temperature - curve[i].temperature;
      float speed_range = curve[i + 1].speed_percent - curve[i].speed_percent;
      float temp_offset = temperature - curve[i].temperature;

      return curve[i].speed_percent +
             (uint8_t)((temp_offset / temp_range) * speed_range);
    }
  }

  return curve[num_points - 1].speed_percent;
}

/* ============================================================================
 * Console Command Implementations
 * ============================================================================
 */

static esp_err_t cmd_fan_status(int argc, char **argv) {
  if (argc == 1) {
    // Show all fans status
    printf("Fan Controller Status:\n");
    printf("======================\n");

    for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
      fan_status_t status;
      esp_err_t ret = fan_controller_get_status(i, &status);
      if (ret == ESP_OK) {
        const char *mode_str;
        switch (status.mode) {
        case FAN_MODE_MANUAL:
          mode_str = "Manual";
          break;
        case FAN_MODE_AUTO_TEMP:
          mode_str = "Auto-Temp";
          break;
        case FAN_MODE_AUTO_CURVE:
          mode_str = "Auto-Curve";
          break;
        case FAN_MODE_OFF:
          mode_str = "Off";
          break;
        default:
          mode_str = "Unknown";
          break;
        }

        printf("Fan %d: %s, %s, Speed: %d%%, Temp: %.1f°C%s\n", status.fan_id,
               status.enabled ? "Enabled" : "Disabled", mode_str,
               status.speed_percent, status.temperature,
               status.fault ? " [FAULT]" : "");
      }
    }
    return ESP_OK;
  }

  // Parse subcommands
  if (strcmp(argv[1], "set") == 0) {
    return cmd_fan_set(argc - 1, &argv[1]);
  } else if (strcmp(argv[1], "mode") == 0) {
    return cmd_fan_mode(argc - 1, &argv[1]);
  } else if (strcmp(argv[1], "enable") == 0) {
    return cmd_fan_enable(argc - 1, &argv[1]);
  } else if (strcmp(argv[1], "gpio") == 0) {
    return cmd_fan_gpio(argc - 1, &argv[1]);
  } else if (strcmp(argv[1], "config") == 0) {
    return cmd_fan_config(argc - 1, &argv[1]);
  } else if (strcmp(argv[1], "status") == 0) {
    return cmd_fan_status(1, argv); // Show status
  } else if (strcmp(argv[1], "help") == 0) {
    return cmd_fan_help();
  } else {
    printf("Unknown command: fan %s\n", argv[1]);
    printf("Type 'fan help' for detailed usage information.\n");
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_fan_set(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: fan set <fan_id> <speed_percent>\n");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t fan_id = (uint8_t)atoi(argv[1]);
  uint8_t speed = (uint8_t)atoi(argv[2]);

  esp_err_t ret = fan_controller_set_speed(fan_id, speed);
  if (ret == ESP_OK) {
    printf("Fan %d speed set to %d%%\n", fan_id, speed);
  } else {
    printf("Failed to set fan %d speed: %s\n", fan_id, esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t cmd_fan_mode(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: fan mode <fan_id> <manual|auto|curve|off>\n");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t fan_id = (uint8_t)atoi(argv[1]);
  fan_mode_t mode;

  if (strcmp(argv[2], "manual") == 0) {
    mode = FAN_MODE_MANUAL;
  } else if (strcmp(argv[2], "auto") == 0) {
    mode = FAN_MODE_AUTO_TEMP;
  } else if (strcmp(argv[2], "curve") == 0) {
    mode = FAN_MODE_AUTO_CURVE;
  } else if (strcmp(argv[2], "off") == 0) {
    mode = FAN_MODE_OFF;
  } else {
    printf("Invalid mode: %s\n", argv[2]);
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = fan_controller_set_mode(fan_id, mode);
  if (ret == ESP_OK) {
    printf("Fan %d mode set to %s\n", fan_id, argv[2]);
  } else {
    printf("Failed to set fan %d mode: %s\n", fan_id, esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t cmd_fan_enable(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: fan enable <fan_id> <on|off>\n");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t fan_id = (uint8_t)atoi(argv[1]);
  bool enable;

  if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "1") == 0) {
    enable = true;
  } else if (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "0") == 0) {
    enable = false;
  } else {
    printf("Invalid enable value: %s (use 'on' or 'off')\n", argv[2]);
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = fan_controller_enable(fan_id, enable);
  if (ret == ESP_OK) {
    printf("Fan %d %s\n", fan_id, enable ? "enabled" : "disabled");
  } else {
    printf("Failed to %s fan %d: %s\n", enable ? "enable" : "disable", fan_id,
           esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t cmd_fan_gpio(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: fan gpio <fan_id> <gpio_pin> [pwm_channel]\n");
    printf("  fan_id: Fan ID (0-%d)\n", s_fan_ctx.num_fans - 1);
    printf("  gpio_pin: GPIO pin number (0-48)\n");
    printf("  pwm_channel: PWM channel (0-7, optional)\n");
    printf("Examples:\n");
    printf("  fan gpio 0 41     # Configure fan 0 to GPIO41 (keep current "
           "channel)\n");
    printf("  fan gpio 0 5 1   # Configure fan 0 to GPIO5, PWM channel 1\n");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t fan_id = (uint8_t)atoi(argv[1]);
  int gpio_pin = atoi(argv[2]);
  int pwm_channel = -1; // Keep current channel by default

  if (argc >= 4) {
    pwm_channel = atoi(argv[3]);
    if (pwm_channel < 0 || pwm_channel >= HAL_PWM_MAX_CHANNEL) {
      printf("Invalid PWM channel: %d (valid range: 0-%d)\n", pwm_channel,
             HAL_PWM_MAX_CHANNEL - 1);
      return ESP_ERR_INVALID_ARG;
    }
  }

  if (fan_id >= s_fan_ctx.num_fans) {
    printf("Invalid fan ID: %d (valid range: 0-%d)\n", fan_id,
           s_fan_ctx.num_fans - 1);
    return ESP_ERR_INVALID_ARG;
  }

  if (gpio_pin < 0 || gpio_pin > 48) {
    printf("Invalid GPIO pin: %d (valid range: 0-48)\n", gpio_pin);
    return ESP_ERR_INVALID_ARG;
  }

  printf("Configuring fan %d to GPIO%d", fan_id, gpio_pin);
  if (pwm_channel >= 0) {
    printf(", PWM channel %d", pwm_channel);
  }
  printf("...\n");

  esp_err_t ret = fan_controller_configure_gpio(fan_id, gpio_pin, pwm_channel);
  if (ret == ESP_OK) {
    printf("Fan %d GPIO configuration updated successfully\n", fan_id);

    // Show updated status
    fan_status_t status;
    if (fan_controller_get_status(fan_id, &status) == ESP_OK) {
      printf("New status: %s, Pin: GPIO%d, Channel: %d\n",
             status.enabled ? "Enabled" : "Disabled",
             s_fan_ctx.fans[fan_id].config.pwm_pin,
             s_fan_ctx.fans[fan_id].config.pwm_channel);
    }
  } else {
    printf("Failed to configure fan %d GPIO: %s\n", fan_id,
           esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t cmd_fan_config(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: fan config <save|load|show|curve> [args...]\n");
    printf("Commands:\n");
    printf(
        "  save [fan_id]  - Save complete fan configuration(s) to storage\n");
    printf("                   (includes hardware config, mode, speed, enable "
           "state)\n");
    printf(
        "  load [fan_id]  - Load complete fan configuration(s) from storage\n");
    printf("  show [fan_id]  - Show current fan configuration(s)\n");
    printf("  curve <fan_id> <temp1:speed1> [temp2:speed2] ... - Set "
           "temperature curve\n");
    printf("Examples:\n");
    printf("  fan config save     # Save all fan configurations with runtime "
           "state\n");
    printf("  fan config save 0   # Save fan 0 complete configuration\n");
    printf("  fan config load     # Load all saved fan configurations\n");
    printf("  fan config show     # Show all current fan configurations\n");
    printf("  fan config curve 0 30:20 50:30 70:40 80:100  # Set curve: "
           "30°C->20%%, 50°C->30%%, 70°C->40%%, 80°C->100%%\n");
    printf("Note: 'save' preserves current mode, speed, and enable state\n");
    return ESP_OK;
  }

  const char *action = argv[1];
  int fan_id = -1; // -1 means all fans

  if (argc >= 3) {
    fan_id = atoi(argv[2]);
    if (fan_id < 0 || fan_id >= s_fan_ctx.num_fans) {
      printf("Invalid fan ID: %d (valid range: 0-%d)\n", fan_id,
             s_fan_ctx.num_fans - 1);
      return ESP_ERR_INVALID_ARG;
    }
  }

  if (strcmp(action, "save") == 0) {
    printf("Saving fan configuration(s) (including runtime parameters)...\n");
    esp_err_t ret = ESP_OK;

    if (fan_id >= 0) {
      ret = save_fan_full_config(fan_id);
      if (ret == ESP_OK) {
        printf("Fan %d configuration saved successfully\n", fan_id);
        // Show what was saved
        fan_status_t status;
        if (fan_controller_get_status(fan_id, &status) == ESP_OK) {
          const char *mode_str =
              (status.mode == FAN_MODE_MANUAL)       ? "Manual"
              : (status.mode == FAN_MODE_AUTO_TEMP)  ? "Auto-Temp"
              : (status.mode == FAN_MODE_AUTO_CURVE) ? "Auto-Curve"
                                                     : "Off";
          printf("  Saved: Mode=%s, Speed=%d%%, Enabled=%s\n", mode_str,
                 status.speed_percent, status.enabled ? "Yes" : "No");
        }
      } else {
        printf("Failed to save fan %d configuration: %s\n", fan_id,
               esp_err_to_name(ret));
      }
    } else {
      // Save all fans
      printf("Saving configurations for %d fans:\n", s_fan_ctx.num_fans);
      for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
        esp_err_t save_ret = save_fan_full_config(i);
        if (save_ret == ESP_OK) {
          printf("  Fan %d: OK\n", i);
        } else {
          printf("  Fan %d: FAILED (%s)\n", i, esp_err_to_name(save_ret));
          if (ret == ESP_OK)
            ret = save_ret;
        }
      }
      if (ret == ESP_OK) {
        printf("All fan configurations saved successfully\n");
      }
    }
    return ret;

  } else if (strcmp(action, "load") == 0) {
    printf("Loading fan configuration(s) (including runtime parameters)...\n");
    esp_err_t ret = ESP_OK;

    if (fan_id >= 0) {
      ret = load_fan_full_config(fan_id);
      if (ret == ESP_OK) {
        printf("Fan %d configuration loaded successfully\n", fan_id);
        // Show what was loaded
        fan_status_t status;
        if (fan_controller_get_status(fan_id, &status) == ESP_OK) {
          const char *mode_str =
              (status.mode == FAN_MODE_MANUAL)       ? "Manual"
              : (status.mode == FAN_MODE_AUTO_TEMP)  ? "Auto-Temp"
              : (status.mode == FAN_MODE_AUTO_CURVE) ? "Auto-Curve"
                                                     : "Off";
          printf("  Loaded: Mode=%s, Speed=%d%%, Enabled=%s\n", mode_str,
                 status.speed_percent, status.enabled ? "Yes" : "No");
        }
      } else {
        printf("Failed to load fan %d configuration: %s\n", fan_id,
               esp_err_to_name(ret));
      }
    } else {
      printf("Loading configurations for %d fans:\n", s_fan_ctx.num_fans);
      for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
        esp_err_t load_ret = load_fan_full_config(i);
        if (load_ret == ESP_OK) {
          printf("  Fan %d: OK\n", i);
        } else {
          printf("  Fan %d: FAILED (%s)\n", i, esp_err_to_name(load_ret));
          if (ret == ESP_OK)
            ret = load_ret;
        }
      }
      if (ret == ESP_OK) {
        printf("All fan configurations loaded successfully\n");
      }
    }
    return ret;

  } else if (strcmp(action, "show") == 0) {
    printf("Fan Configuration(s):\n");
    printf("=====================\n");

    if (fan_id >= 0) {
      // Show specific fan with complete information
      fan_instance_t *fan = &s_fan_ctx.fans[fan_id];
      printf("Fan %d:\n", fan_id);
      printf("  Hardware Configuration:\n");
      printf("    GPIO Pin: %d\n", fan->config.pwm_pin);
      printf("    PWM Channel: %d\n", fan->config.pwm_channel);
      printf("    PWM Timer: %d\n", fan->config.pwm_timer);
      printf("    PWM Inverted: %s\n", fan->config.invert_pwm ? "Yes" : "No");
      printf("  Current Status:\n");
      const char *mode_str =
          (fan->status.mode == FAN_MODE_MANUAL)       ? "Manual"
          : (fan->status.mode == FAN_MODE_AUTO_TEMP)  ? "Auto-Temp"
          : (fan->status.mode == FAN_MODE_AUTO_CURVE) ? "Auto-Curve"
                                                      : "Off";
      printf("    Mode: %s\n", mode_str);
      printf("    Speed: %d%%\n", fan->status.speed_percent);
      printf("    Enabled: %s\n", fan->status.enabled ? "Yes" : "No");
      printf("    Temperature: %.1f°C\n", fan->status.temperature);
      printf("    Fault: %s\n", fan->status.fault ? "Yes" : "No");
      printf("  Temperature Curve:\n");
      if (fan->curve_enabled && fan->num_curve_points > 0) {
        printf("    Enabled: Yes (%d points)\n", fan->num_curve_points);
        for (uint8_t j = 0; j < fan->num_curve_points; j++) {
          printf("    %.1f°C -> %d%%\n", fan->curve_points[j].temperature,
                 fan->curve_points[j].speed_percent);
        }
      } else {
        printf("    Enabled: No\n");
      }
    } else {
      // Show all fans with summary information
      for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
        fan_instance_t *fan = &s_fan_ctx.fans[i];
        const char *mode_str =
            (fan->status.mode == FAN_MODE_MANUAL)       ? "Manual"
            : (fan->status.mode == FAN_MODE_AUTO_TEMP)  ? "Auto-Temp"
            : (fan->status.mode == FAN_MODE_AUTO_CURVE) ? "Auto-Curve"
                                                        : "Off";
        printf("Fan %d: GPIO%d, Ch%d, %s, Speed%d%%, %s\n", i,
               fan->config.pwm_pin, fan->config.pwm_channel, mode_str,
               fan->status.speed_percent,
               fan->status.enabled ? "Enabled" : "Disabled");
        if (fan->curve_enabled && fan->num_curve_points > 0) {
          printf("       Curve: %d points configured\n", fan->num_curve_points);
        }
      }
    }
    return ESP_OK;

  } else if (strcmp(action, "curve") == 0) {
    if (argc < 4) {
      printf("Usage: fan config curve <fan_id> <temp1:speed1> [temp2:speed2] "
             "...\n");
      printf("Example: fan config curve 0 30:20 50:30 70:40 80:100\n");
      printf("  Sets temperature curve: 30°C->20%%, 50°C->30%%, 70°C->40%%, "
             "80°C->100%%\n");
      return ESP_ERR_INVALID_ARG;
    }

    uint8_t fan_id = (uint8_t)atoi(argv[2]);
    if (fan_id >= s_fan_ctx.num_fans) {
      printf("Invalid fan ID: %d (valid range: 0-%d)\n", fan_id,
             s_fan_ctx.num_fans - 1);
      return ESP_ERR_INVALID_ARG;
    }

    uint8_t num_points = argc - 3; // Number of curve points
    if (num_points < 2 || num_points > 10) {
      printf("Invalid number of curve points: %d (must be 2-10)\n", num_points);
      return ESP_ERR_INVALID_ARG;
    }

    fan_curve_point_t *curve_points =
        (fan_curve_point_t *)malloc(sizeof(fan_curve_point_t) * num_points);
    if (curve_points == NULL) {
      printf("Failed to allocate memory for curve points\n");
      return ESP_ERR_NO_MEM;
    }

    // Parse curve points
    bool parse_error = false;
    for (uint8_t i = 0; i < num_points; i++) {
      char *point_str = (char *)argv[3 + i];
      char *colon_pos = strchr(point_str, ':');

      if (colon_pos == NULL) {
        printf("Invalid curve point format: %s (expected temp:speed)\n",
               point_str);
        parse_error = true;
        break;
      }

      *colon_pos = '\0'; // Split the string
      float temp = atof(point_str);
      uint8_t speed = (uint8_t)atoi(colon_pos + 1);

      if (temp < -50.0f || temp > 150.0f) {
        printf("Invalid temperature: %.1f°C (must be -50°C to 150°C)\n", temp);
        parse_error = true;
        break;
      }

      if (speed > 100) {
        printf("Invalid speed: %d%% (must be 0-100%%)\n", speed);
        parse_error = true;
        break;
      }

      curve_points[i].temperature = temp;
      curve_points[i].speed_percent = speed;
    }

    if (parse_error) {
      free(curve_points);
      return ESP_ERR_INVALID_ARG;
    }

    // Set the curve
    esp_err_t ret = fan_controller_set_curve(fan_id, curve_points, num_points);

    if (ret == ESP_OK) {
      printf("Fan %d temperature curve configured with %d points:\n", fan_id,
             num_points);
      for (uint8_t i = 0; i < num_points; i++) {
        printf("  %.1f°C -> %d%%\n", curve_points[i].temperature,
               curve_points[i].speed_percent);
      }
      printf("Set fan mode to 'curve' to activate: fan mode %d curve\n",
             fan_id);
    } else {
      printf("Failed to set fan %d curve: %s\n", fan_id, esp_err_to_name(ret));
    }

    free(curve_points);

    return ret;

  } else {
    printf("Unknown config action: %s\n", action);
    printf("Valid actions: save, load, show, curve\n");
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_fan_help(void) {
  printf("\n");
  printf("Fan Controller Command Reference\n");
  printf("================================\n");
  printf("\n");
  printf("SYNOPSIS\n");
  printf("  fan <command> [options...]\n");
  printf("\n");
  printf("DESCRIPTION\n");
  printf("  The fan command provides comprehensive control over PWM fans.\n");
  printf("  Supports up to 4 fans with manual/automatic speed control,\n");
  printf("  temperature curves, GPIO configuration, and persistent storage.\n");
  printf("\n");
  printf("COMMANDS\n");
  printf("\n");
  printf("  status\n");
  printf("    Show status of all configured fans\n");
  printf("    Displays: ID, Enable state, Mode, Speed, Temperature, Faults\n");
  printf("\n");
  printf("  set <fan_id> <speed>\n");
  printf("    Set manual fan speed\n");
  printf("    fan_id: Fan ID (0-3)\n");
  printf("    speed:  Speed percentage (0-100)\n");
  printf("    Note: Automatically switches fan to manual mode\n");
  printf("\n");
  printf("  mode <fan_id> <mode>\n");
  printf("    Set fan control mode\n");
  printf("    fan_id: Fan ID (0-3)\n");
  printf("    mode:   manual  - Manual speed control\n");
  printf("            auto    - Temperature-based automatic control\n");
  printf("            curve   - Custom temperature curve control\n");
  printf("            off     - Fan disabled\n");
  printf("\n");
  printf("  enable <fan_id> <state>\n");
  printf("    Enable or disable a fan\n");
  printf("    fan_id: Fan ID (0-3)\n");
  printf("    state:  on|1|enable  - Enable fan\n");
  printf("            off|0|disable - Disable fan\n");
  printf("\n");
  printf("  gpio <fan_id> <pin> [channel]\n");
  printf("    Configure GPIO pin and PWM channel\n");
  printf("    fan_id:  Fan ID (0-3)\n");
  printf("    pin:     GPIO pin number (0-48)\n");
  printf("    channel: PWM channel (0-7, optional)\n");
  printf("    Note: Configuration is automatically saved to NVS\n");
  printf("\n");
  printf("  config <action> [args...]\n");
  printf("    Configuration management\n");
  printf("\n");
  printf("    save [fan_id]   - Save complete configuration to NVS\n");
  printf("                      Includes hardware config, mode, speed, enable "
         "state\n");
  printf("                      If fan_id omitted, saves all fans\n");
  printf("\n");
  printf("    load [fan_id]   - Load complete configuration from NVS\n");
  printf("                      If fan_id omitted, loads all fans\n");
  printf("\n");
  printf("    show [fan_id]   - Display current configuration\n");
  printf("                      If fan_id omitted, shows all fans\n");
  printf("\n");
  printf("    curve <fan_id> <temp1:speed1> [temp2:speed2] ...\n");
  printf("                    - Configure temperature curve (2-10 points)\n");
  printf("                      temp: Temperature in Celsius (-50 to 150)\n");
  printf("                      speed: Fan speed percentage (0-100)\n");
  printf(
      "                      Points are automatically sorted by temperature\n");
  printf("\n");
  printf("  help\n");
  printf("    Display this help information\n");
  printf("\n");
  printf("EXAMPLES\n");
  printf("\n");
  printf("  # Show all fans status\n");
  printf("  fan status\n");
  printf("\n");
  printf("  # Configure fan 0 on GPIO 41\n");
  printf("  fan gpio 0 41\n");
  printf("\n");
  printf("  # Set fan 0 to 75%% speed (manual mode)\n");
  printf("  fan set 0 75\n");
  printf("\n");
  printf("  # Enable fan 1\n");
  printf("  fan enable 1 on\n");
  printf("\n");
  printf("  # Set fan 0 to curve mode\n");
  printf("  fan mode 0 curve\n");
  printf("\n");
  printf("  # Configure temperature curve for fan 0\n");
  printf("  fan config curve 0 30:20 50:30 70:40 80:100\n");
  printf("\n");
  printf("  # Save all fan configurations\n");
  printf("  fan config save\n");
  printf("\n");
  printf("  # Temperature control examples\n");
  printf("  temp set 45     # Set manual test temperature to 45°C\n");
  printf("  temp auto       # Switch to AGX automatic mode\n");
  printf("  temp status     # Check current temperature source\n");
  printf("  fan mode 0 curve  # Fan will follow temperature source\n");
  printf("\n");
  printf("NOTES\n");
  printf("\n");
  printf("  • Configurations are automatically saved to NVS flash\n");
  printf("  • Fan settings persist across system reboots\n");
  printf("  • PWM frequency: 25kHz, Resolution: 10-bit\n");
  printf("  • Temperature curves use linear interpolation\n");
  printf("  • Temperature sources: Manual (temp set), AGX CPU (temp auto), "
         "Default\n");
  printf(
      "  • Use 'temp' commands to control temperature input for curve mode\n");
  printf("  • GPIO pins must support PWM output (check ESP32-S3 datasheet)\n");
  printf("\n");

  return ESP_OK;
}

/* ============================================================================
 * NVS Configuration Functions
 * ============================================================================
 */

static esp_err_t save_fan_config(uint8_t fan_id) {
  // This function now saves only hardware configuration (for GPIO changes)
  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  char key[32];
  snprintf(key, sizeof(key), "fan_%d_hw", fan_id);

  // Save fan hardware configuration as a blob using config manager
  esp_err_t ret =
      config_manager_set(FAN_CONFIG_NAMESPACE, key, CONFIG_TYPE_BLOB,
                         &s_fan_ctx.fans[fan_id].config, sizeof(fan_config_t));

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Fan %d hardware configuration saved", fan_id);
    // Commit immediately for critical configuration
    config_manager_commit();
  } else {
    ESP_LOGE(TAG, "Failed to save fan %d hardware config: %s", fan_id,
             esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t save_fan_full_config(uint8_t fan_id) {
  // This function saves complete configuration including runtime parameters
  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  fan_full_config_t full_config = {
      .hardware_config = s_fan_ctx.fans[fan_id].config,
      .current_mode = s_fan_ctx.fans[fan_id].status.mode,
      .current_speed = s_fan_ctx.fans[fan_id].status.speed_percent,
      .enabled = s_fan_ctx.fans[fan_id].status.enabled,
      .num_curve_points = s_fan_ctx.fans[fan_id].num_curve_points,
      .curve_enabled = s_fan_ctx.fans[fan_id].curve_enabled,
      .version = 2 // Configuration version for future compatibility
  };

  // Copy curve points if they exist
  if (s_fan_ctx.fans[fan_id].curve_points &&
      s_fan_ctx.fans[fan_id].num_curve_points > 0) {
    memcpy(full_config.curve_points, s_fan_ctx.fans[fan_id].curve_points,
           sizeof(fan_curve_point_t) * s_fan_ctx.fans[fan_id].num_curve_points);
  }

  char key[32];
  snprintf(key, sizeof(key), "fan_%d_full", fan_id);

  esp_err_t ret =
      config_manager_set(FAN_CONFIG_NAMESPACE, key, CONFIG_TYPE_BLOB,
                         &full_config, sizeof(fan_full_config_t));

  if (ret == ESP_OK) {
    ESP_LOGI(
        TAG,
        "Fan %d full configuration saved (Mode:%d, Speed:%d%%, Enabled:%s)",
        fan_id, full_config.current_mode, full_config.current_speed,
        full_config.enabled ? "Yes" : "No");
    config_manager_commit();
  } else {
    ESP_LOGE(TAG, "Failed to save fan %d full config: %s", fan_id,
             esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t load_fan_config(uint8_t fan_id) {
  // This function loads only hardware configuration
  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  char key[32];
  snprintf(key, sizeof(key), "fan_%d_hw", fan_id);

  size_t config_size = sizeof(fan_config_t);
  esp_err_t ret =
      config_manager_get(FAN_CONFIG_NAMESPACE, key, CONFIG_TYPE_BLOB,
                         &s_fan_ctx.fans[fan_id].config, &config_size);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Fan %d hardware configuration loaded: GPIO%d, Channel:%d",
             fan_id, s_fan_ctx.fans[fan_id].config.pwm_pin,
             s_fan_ctx.fans[fan_id].config.pwm_channel);

    // Re-configure PWM with loaded settings
    if (s_fan_ctx.fans[fan_id].config.pwm_pin >= 0) {
      hal_pwm_config_t pwm_config = {
          .channel = s_fan_ctx.fans[fan_id].config.pwm_channel,
          .pin = (gpio_num_t)s_fan_ctx.fans[fan_id].config.pwm_pin,
          .timer = s_fan_ctx.fans[fan_id].config.pwm_timer,
          .frequency = FAN_CONTROLLER_PWM_FREQUENCY,
          .resolution = FAN_CONTROLLER_PWM_RESOLUTION,
          .duty_cycle = (s_fan_ctx.fans[fan_id].config.default_speed *
                         ((1 << FAN_CONTROLLER_PWM_RESOLUTION) - 1)) /
                        100,
          .invert = s_fan_ctx.fans[fan_id].config.invert_pwm};

      esp_err_t pwm_ret = hal_pwm_configure(&pwm_config);
      if (pwm_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure PWM for fan %d: %s", fan_id,
                 esp_err_to_name(pwm_ret));
        s_fan_ctx.fans[fan_id].status.enabled = false;
        s_fan_ctx.fans[fan_id].status.fault = true;
      } else {
        s_fan_ctx.fans[fan_id].status.enabled = true;
        s_fan_ctx.fans[fan_id].status.fault = false;
      }
    }

  } else if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGI(TAG,
             "No saved hardware configuration found for fan %d, initializing "
             "with defaults",
             fan_id);
    // Initialize default PWM if pin is configured
    if (s_fan_ctx.fans[fan_id].config.pwm_pin >= 0) {
      hal_pwm_config_t pwm_config = {
          .channel = s_fan_ctx.fans[fan_id].config.pwm_channel,
          .pin = (gpio_num_t)s_fan_ctx.fans[fan_id].config.pwm_pin,
          .timer = s_fan_ctx.fans[fan_id].config.pwm_timer,
          .frequency = FAN_CONTROLLER_PWM_FREQUENCY,
          .resolution = FAN_CONTROLLER_PWM_RESOLUTION,
          .duty_cycle = (s_fan_ctx.fans[fan_id].config.default_speed *
                         ((1 << FAN_CONTROLLER_PWM_RESOLUTION) - 1)) /
                        100,
          .invert = s_fan_ctx.fans[fan_id].config.invert_pwm};

      esp_err_t pwm_ret = hal_pwm_configure(&pwm_config);
      if (pwm_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize default PWM for fan %d: %s", fan_id,
                 esp_err_to_name(pwm_ret));
        s_fan_ctx.fans[fan_id].status.enabled = false;
        s_fan_ctx.fans[fan_id].status.fault = true;
      } else {
        ESP_LOGI(TAG, "Fan %d initialized with default PWM: GPIO%d, Channel %d",
                 fan_id, s_fan_ctx.fans[fan_id].config.pwm_pin,
                 s_fan_ctx.fans[fan_id].config.pwm_channel);
        s_fan_ctx.fans[fan_id].status.enabled = true;
        s_fan_ctx.fans[fan_id].status.fault = false;
        // Initial PWM duty cycle will be set by the fan controller task
      }
    }
    ret = ESP_OK; // Not an error, use default config
  } else {
    ESP_LOGE(TAG, "Failed to load fan %d hardware config: %s", fan_id,
             esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t load_fan_full_config(uint8_t fan_id) {
  // This function loads complete configuration including runtime parameters
  if (fan_id >= s_fan_ctx.num_fans) {
    return ESP_ERR_INVALID_ARG;
  }

  char key[32];
  snprintf(key, sizeof(key), "fan_%d_full", fan_id);

  fan_full_config_t full_config;
  size_t config_size = sizeof(fan_full_config_t);
  esp_err_t ret = config_manager_get(
      FAN_CONFIG_NAMESPACE, key, CONFIG_TYPE_BLOB, &full_config, &config_size);

  if (ret == ESP_OK) {
    // Check version compatibility
    if (full_config.version < 1 || full_config.version > 2) {
      ESP_LOGW(TAG,
               "Fan %d config version mismatch, using hardware config only",
               fan_id);
      return load_fan_config(fan_id);
    }

    // Load hardware configuration
    s_fan_ctx.fans[fan_id].config = full_config.hardware_config;

    // Load runtime state
    s_fan_ctx.fans[fan_id].status.mode = full_config.current_mode;
    s_fan_ctx.fans[fan_id].status.speed_percent = full_config.current_speed;
    s_fan_ctx.fans[fan_id].status.enabled = full_config.enabled;

    // Load curve data if version 2 or higher
    if (full_config.version >= 2) {
      // Free existing curve points
      if (s_fan_ctx.fans[fan_id].curve_points) {
        free(s_fan_ctx.fans[fan_id].curve_points);
        s_fan_ctx.fans[fan_id].curve_points = NULL;
      }

      s_fan_ctx.fans[fan_id].num_curve_points = full_config.num_curve_points;
      s_fan_ctx.fans[fan_id].curve_enabled = full_config.curve_enabled;

      if (full_config.num_curve_points > 0 && full_config.curve_enabled) {
        s_fan_ctx.fans[fan_id].curve_points =
            malloc(sizeof(fan_curve_point_t) * full_config.num_curve_points);
        if (s_fan_ctx.fans[fan_id].curve_points) {
          memcpy(s_fan_ctx.fans[fan_id].curve_points, full_config.curve_points,
                 sizeof(fan_curve_point_t) * full_config.num_curve_points);
          ESP_LOGI(TAG, "Fan %d temperature curve loaded with %d points",
                   fan_id, full_config.num_curve_points);
        } else {
          ESP_LOGE(TAG, "Failed to allocate memory for fan %d curve points",
                   fan_id);
          s_fan_ctx.fans[fan_id].num_curve_points = 0;
          s_fan_ctx.fans[fan_id].curve_enabled = false;
        }
      }
    }

    ESP_LOGI(
        TAG,
        "Fan %d full configuration loaded: GPIO%d, Mode:%d, Speed:%d%%, "
        "Enabled:%s, Curve:%s (%d points)",
        fan_id, full_config.hardware_config.pwm_pin, full_config.current_mode,
        full_config.current_speed, full_config.enabled ? "Yes" : "No",
        full_config.curve_enabled ? "Yes" : "No", full_config.num_curve_points);

    // Re-configure PWM with loaded settings
    if (s_fan_ctx.fans[fan_id].config.pwm_pin >= 0) {
      hal_pwm_config_t pwm_config = {
          .channel = s_fan_ctx.fans[fan_id].config.pwm_channel,
          .pin = (gpio_num_t)s_fan_ctx.fans[fan_id].config.pwm_pin,
          .timer = s_fan_ctx.fans[fan_id].config.pwm_timer,
          .frequency = FAN_CONTROLLER_PWM_FREQUENCY,
          .resolution = FAN_CONTROLLER_PWM_RESOLUTION,
          .duty_cycle = (full_config.current_speed *
                         ((1 << FAN_CONTROLLER_PWM_RESOLUTION) - 1)) /
                        100,
          .invert = s_fan_ctx.fans[fan_id].config.invert_pwm};

      esp_err_t pwm_ret = hal_pwm_configure(&pwm_config);
      if (pwm_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure PWM for fan %d: %s", fan_id,
                 esp_err_to_name(pwm_ret));
        s_fan_ctx.fans[fan_id].status.enabled = false;
        s_fan_ctx.fans[fan_id].status.fault = true;
      } else {
        s_fan_ctx.fans[fan_id].status.fault = false;
        // Defer PWM speed setting to avoid LEDC not initialized error
        // The speed will be applied by the fan controller task
      }
    }

  } else if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGI(
        TAG,
        "No saved full configuration found for fan %d, trying hardware config",
        fan_id);
    return load_fan_config(fan_id); // Fallback to hardware config which will
                                    // init defaults if needed
  } else {
    ESP_LOGE(TAG, "Failed to load fan %d full config: %s", fan_id,
             esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t load_all_fan_configs(void) {
  esp_err_t ret = ESP_OK;

  // Wait briefly to ensure LEDC peripheral is fully initialized
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "Loading fan configurations at startup...");
  for (uint8_t i = 0; i < s_fan_ctx.num_fans; i++) {
    // Try to load full configuration first, fallback to hardware config
    esp_err_t load_ret = load_fan_full_config(i);
    if (load_ret != ESP_OK && ret == ESP_OK) {
      ret = load_ret; // Return first error encountered
    }
  }

  return ret;
}