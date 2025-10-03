/**
 * @file usb_mux_controller.c
 * @brief USB MUX Controller Component Implementation
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#include "usb_mux_controller.h"
#include "esp_log.h"
#include "gpio_controller.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

/* ============================================================================
 * Private Constants and Macros
 * ============================================================================
 */

#define DEFAULT_SWITCH_DELAY_MS 10 ///< Default delay between MUX pin changes

/* ============================================================================
 * Private Type Definitions
 * ============================================================================
 */

/**
 * @brief USB MUX controller internal state
 */
typedef struct {
  bool initialized;                ///< Initialization status
  usb_mux_target_t current_target; ///< Current target
  usb_mux_config_t config;         ///< Configuration
  uint32_t switch_count;           ///< Switch counter
  SemaphoreHandle_t mutex;         ///< Mutex for thread safety
} usb_mux_controller_state_t;

/* ============================================================================
 * Private Variables
 * ============================================================================
 */

static usb_mux_controller_state_t s_usb_mux_state = {0};
static const char *TAG = USB_MUX_CONTROLLER_TAG;

/**
 * @brief Default USB MUX configuration
 */
static const usb_mux_config_t s_default_config = {
    .default_target = USB_MUX_TARGET_ESP32S3,
    .auto_restore = true,
    .switch_delay_ms = DEFAULT_SWITCH_DELAY_MS,
};

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

static esp_err_t set_mux_pins(usb_mux_target_t target);
static esp_err_t validate_configuration(const usb_mux_config_t *config);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

esp_err_t usb_mux_controller_init(void) {
  return usb_mux_controller_init_with_config(&s_default_config);
}

esp_err_t usb_mux_controller_init_with_config(const usb_mux_config_t *config) {
  if (s_usb_mux_state.initialized) {
    ESP_LOGW(TAG, "USB MUX controller already initialized");
    return ESP_OK;
  }

  if (config == NULL) {
    ESP_LOGE(TAG, "Configuration is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Validate configuration
  esp_err_t ret = validate_configuration(config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Invalid configuration");
    return ret;
  }

  // Check if GPIO controller is available
  if (!gpio_controller_is_initialized()) {
    ESP_LOGE(TAG, "GPIO controller is not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Create mutex
  s_usb_mux_state.mutex = xSemaphoreCreateMutex();
  if (s_usb_mux_state.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }

  // Copy configuration
  memcpy(&s_usb_mux_state.config, config, sizeof(usb_mux_config_t));
  s_usb_mux_state.switch_count = 0;
  s_usb_mux_state.initialized = true;

  // Set default target if auto_restore is enabled
  if (config->auto_restore) {
    ret = usb_mux_controller_set_target(config->default_target);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set default target");
      usb_mux_controller_deinit();
      return ret;
    }
  } else {
    s_usb_mux_state.current_target = config->default_target;
  }

  ESP_LOGI(TAG, "USB MUX controller initialized successfully (default: %s)",
           usb_mux_controller_get_target_name(config->default_target));
  return ESP_OK;
}

esp_err_t usb_mux_controller_deinit(void) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGW(TAG, "USB MUX controller not initialized");
    return ESP_OK;
  }

  // Take mutex before cleanup
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    s_usb_mux_state.initialized = false;
    xSemaphoreGive(s_usb_mux_state.mutex);
  }

  // Delete mutex
  if (s_usb_mux_state.mutex != NULL) {
    vSemaphoreDelete(s_usb_mux_state.mutex);
    s_usb_mux_state.mutex = NULL;
  }

  ESP_LOGI(TAG, "USB MUX controller deinitialized");
  return ESP_OK;
}

bool usb_mux_controller_is_initialized(void) {
  return s_usb_mux_state.initialized;
}

esp_err_t usb_mux_controller_set_target(usb_mux_target_t target) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGE(TAG, "USB MUX controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = usb_mux_controller_validate_target(target);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Invalid target: %d", target);
    return ret;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  // Check if already at target
  if (s_usb_mux_state.current_target == target) {
    ESP_LOGD(TAG, "Already at target %s",
             usb_mux_controller_get_target_name(target));
    xSemaphoreGive(s_usb_mux_state.mutex);
    return ESP_OK;
  }

  // Set MUX pins
  ret = set_mux_pins(target);
  if (ret == ESP_OK) {
    s_usb_mux_state.current_target = target;
    s_usb_mux_state.switch_count++;
    ESP_LOGI(TAG, "USB-C interface switched to %s",
             usb_mux_controller_get_target_name(target));
  } else {
    ESP_LOGE(TAG, "Failed to switch to %s: %s",
             usb_mux_controller_get_target_name(target), esp_err_to_name(ret));
  }

  xSemaphoreGive(s_usb_mux_state.mutex);
  return ret;
}

esp_err_t usb_mux_controller_get_target(usb_mux_target_t *target) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGE(TAG, "USB MUX controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (target == NULL) {
    ESP_LOGE(TAG, "Target pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  *target = s_usb_mux_state.current_target;

  xSemaphoreGive(s_usb_mux_state.mutex);
  return ESP_OK;
}

const char *usb_mux_controller_get_target_name(usb_mux_target_t target) {
  switch (target) {
  case USB_MUX_TARGET_ESP32S3:
    return "ESP32S3";
  case USB_MUX_TARGET_AGX:
    return "AGX";
  case USB_MUX_TARGET_LPMU:
    return "LPMU";
  default:
    return "Unknown";
  }
}

esp_err_t usb_mux_controller_get_status(usb_mux_status_t *status) {
  if (!s_usb_mux_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  status->initialized = s_usb_mux_state.initialized;
  status->current_target = s_usb_mux_state.current_target;
  status->switch_count = s_usb_mux_state.switch_count;
  status->last_switch_time = xTaskGetTickCount();

  xSemaphoreGive(s_usb_mux_state.mutex);
  return ESP_OK;
}

esp_err_t usb_mux_controller_validate_target(usb_mux_target_t target) {
  if (target >= USB_MUX_TARGET_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

esp_err_t usb_mux_controller_reset_to_default(void) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGE(TAG, "USB MUX controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  return usb_mux_controller_set_target(s_usb_mux_state.config.default_target);
}

const usb_mux_config_t *usb_mux_controller_get_default_config(void) {
  return &s_default_config;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static esp_err_t set_mux_pins(usb_mux_target_t target) {
  gpio_state_t mux1_state, mux2_state;

  // Determine MUX pin states based on target
  switch (target) {
  case USB_MUX_TARGET_ESP32S3: // mux1=0, mux2=0
    mux1_state = GPIO_STATE_LOW;
    mux2_state = GPIO_STATE_LOW;
    break;
  case USB_MUX_TARGET_AGX: // mux1=1, mux2=0
    mux1_state = GPIO_STATE_HIGH;
    mux2_state = GPIO_STATE_LOW;
    break;
  case USB_MUX_TARGET_LPMU: // mux1=1, mux2=1
    mux1_state = GPIO_STATE_HIGH;
    mux2_state = GPIO_STATE_HIGH;
    break;
  default:
    ESP_LOGE(TAG, "Invalid USB MUX target: %d", target);
    return ESP_ERR_INVALID_ARG;
  }

  // Set MUX1 pin
  esp_err_t ret = gpio_controller_set_output(USB_MUX1_PIN, mux1_state);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set MUX1 pin (GPIO%d): %s", USB_MUX1_PIN,
             esp_err_to_name(ret));
    return ret;
  }

  // Add delay between pin changes if configured
  if (s_usb_mux_state.config.switch_delay_ms > 0) {
    vTaskDelay(pdMS_TO_TICKS(s_usb_mux_state.config.switch_delay_ms));
  }

  // Set MUX2 pin
  ret = gpio_controller_set_output(USB_MUX2_PIN, mux2_state);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set MUX2 pin (GPIO%d): %s", USB_MUX2_PIN,
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGD(TAG, "MUX pins set: MUX1(GPIO%d)=%d, MUX2(GPIO%d)=%d", USB_MUX1_PIN,
           mux1_state, USB_MUX2_PIN, mux2_state);

  return ESP_OK;
}

static esp_err_t validate_configuration(const usb_mux_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Validate default target
  if (usb_mux_controller_validate_target(config->default_target) != ESP_OK) {
    ESP_LOGE(TAG, "Invalid default target: %d", config->default_target);
    return ESP_ERR_INVALID_ARG;
  }

  // Validate switch delay (reasonable range: 0-1000ms)
  if (config->switch_delay_ms > 1000) {
    ESP_LOGE(TAG, "Switch delay too large: %lu ms", config->switch_delay_ms);
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}