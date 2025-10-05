/**
 * @#include \"usb_mux_controller.h\"
#include \"config_manager.h\"
#include \"esp_log.h\"
#include \"gpio_controller.h\"
#include \"nvs.h\"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>_mux_controller.c
 * @brief USB MUX Controller Component Implementation
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#include "usb_mux_controller.h"
#include "config_manager.h"
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

/* NVS configuration keys */
#define USB_MUX_CONFIG_NAMESPACE "usb_mux"
#define USB_MUX_CONFIG_KEY_TARGET "current_target"

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

  // Set default target (config restoration will be handled later via
  // load_config)
  usb_mux_target_t target_to_set = config->default_target;

  // Set the default target first
  ret = set_mux_pins(target_to_set);
  if (ret == ESP_OK) {
    s_usb_mux_state.current_target = target_to_set;
    ESP_LOGI(TAG, "USB MUX initialized with default target: %s",
             usb_mux_controller_get_target_name(target_to_set));
    if (config->auto_restore) {
      ESP_LOGI(
          TAG,
          "Auto-restore enabled - saved configuration will be loaded later");
    }
  } else {
    ESP_LOGE(TAG, "Failed to set default target during initialization");
    usb_mux_controller_deinit();
    return ret;
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

    // Auto-save configuration to NVS
    uint8_t target_u8 = (uint8_t)target;
    esp_err_t save_ret =
        config_manager_set(USB_MUX_CONFIG_NAMESPACE, USB_MUX_CONFIG_KEY_TARGET,
                           CONFIG_TYPE_UINT8, &target_u8, sizeof(uint8_t));
    if (save_ret == ESP_OK) {
      ESP_LOGD(TAG, "USB MUX configuration auto-saved");
    } else {
      ESP_LOGW(TAG, "Failed to auto-save USB MUX configuration: %s",
               esp_err_to_name(save_ret));
    }
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

esp_err_t usb_mux_controller_save_config(void) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGE(TAG, "USB MUX controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  // Save current target to NVS
  uint8_t target_u8 = (uint8_t)s_usb_mux_state.current_target;
  esp_err_t ret =
      config_manager_set(USB_MUX_CONFIG_NAMESPACE, USB_MUX_CONFIG_KEY_TARGET,
                         CONFIG_TYPE_UINT8, &target_u8, sizeof(uint8_t));

  if (ret == ESP_OK) {
    ESP_LOGI(
        TAG, "USB MUX configuration saved: target=%s",
        usb_mux_controller_get_target_name(s_usb_mux_state.current_target));
  } else {
    ESP_LOGE(TAG, "Failed to save USB MUX configuration: %s",
             esp_err_to_name(ret));
  }

  xSemaphoreGive(s_usb_mux_state.mutex);
  return ret;
}

esp_err_t usb_mux_controller_load_config(void) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGE(TAG, "USB MUX controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  // Load target from NVS
  uint8_t target_u8;
  size_t size = sizeof(uint8_t);
  esp_err_t ret =
      config_manager_get(USB_MUX_CONFIG_NAMESPACE, USB_MUX_CONFIG_KEY_TARGET,
                         CONFIG_TYPE_UINT8, &target_u8, &size);

  if (ret == ESP_OK) {
    usb_mux_target_t target = (usb_mux_target_t)target_u8;

    // Validate loaded target
    if (usb_mux_controller_validate_target(target) == ESP_OK) {
      // Set the target without saving again (to avoid recursive save)
      esp_err_t set_ret = set_mux_pins(target);
      if (set_ret == ESP_OK) {
        s_usb_mux_state.current_target = target;
        ESP_LOGI(TAG, "USB MUX configuration loaded: target=%s",
                 usb_mux_controller_get_target_name(target));
      } else {
        ESP_LOGE(TAG, "Failed to set loaded target: %s",
                 esp_err_to_name(set_ret));
        ret = set_ret;
      }
    } else {
      ESP_LOGE(TAG, "Invalid target loaded from NVS: %d", target);
      ret = ESP_ERR_INVALID_ARG;
    }
  } else if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "No saved USB MUX configuration found, using default");
    ret = ESP_ERR_NOT_FOUND;
  } else {
    ESP_LOGE(TAG, "Failed to load USB MUX configuration: %s",
             esp_err_to_name(ret));
  }

  xSemaphoreGive(s_usb_mux_state.mutex);
  return ret;
}

esp_err_t usb_mux_controller_verify_target(bool *target_verified) {
  if (!s_usb_mux_state.initialized) {
    ESP_LOGE(TAG, "USB MUX controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_usb_mux_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  usb_mux_target_t current_target = s_usb_mux_state.current_target;
  bool verified = false;
  esp_err_t ret = ESP_OK;

  // Determine expected GPIO states for current target
  gpio_state_t expected_mux1, expected_mux2;
  switch (current_target) {
  case USB_MUX_TARGET_ESP32S3:
    expected_mux1 = GPIO_STATE_LOW;
    expected_mux2 = GPIO_STATE_LOW;
    break;
  case USB_MUX_TARGET_AGX:
    expected_mux1 = GPIO_STATE_HIGH;
    expected_mux2 = GPIO_STATE_LOW;
    break;
  case USB_MUX_TARGET_LPMU:
    expected_mux1 = GPIO_STATE_HIGH;
    expected_mux2 = GPIO_STATE_HIGH;
    break;
  default:
    ESP_LOGE(TAG, "Invalid current target: %d", current_target);
    ret = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }

  // Read actual GPIO states
  gpio_state_t actual_mux1, actual_mux2;

  // Note: Reading GPIO pins in output mode can interfere with the output
  // For MUX pins, we'll temporarily switch to input mode to read safely
  ret = gpio_controller_read_input(USB_MUX1_PIN, &actual_mux1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read MUX1 pin (GPIO%d): %s", USB_MUX1_PIN,
             esp_err_to_name(ret));
    goto cleanup;
  }

  ret = gpio_controller_read_input(USB_MUX2_PIN, &actual_mux2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read MUX2 pin (GPIO%d): %s", USB_MUX2_PIN,
             esp_err_to_name(ret));
    goto cleanup;
  }

  // Restore output states after reading (in case input mode changed them)
  esp_err_t restore_ret1 =
      gpio_controller_set_output(USB_MUX1_PIN, expected_mux1);
  esp_err_t restore_ret2 =
      gpio_controller_set_output(USB_MUX2_PIN, expected_mux2);

  if (restore_ret1 != ESP_OK || restore_ret2 != ESP_OK) {
    ESP_LOGW(
        TAG,
        "Warning: Failed to restore GPIO output states after verification");
  }

  // Compare expected vs actual states
  if (actual_mux1 == expected_mux1 && actual_mux2 == expected_mux2) {
    verified = true;
    ESP_LOGD(TAG, "Target verification PASSED: %s (MUX1=%d, MUX2=%d)",
             usb_mux_controller_get_target_name(current_target), actual_mux1,
             actual_mux2);
  } else {
    verified = false;
    ESP_LOGW(TAG,
             "Target verification FAILED: %s - Expected(MUX1=%d, MUX2=%d), "
             "Actual(MUX1=%d, MUX2=%d)",
             usb_mux_controller_get_target_name(current_target), expected_mux1,
             expected_mux2, actual_mux1, actual_mux2);
    ret = ESP_ERR_INVALID_STATE;
  }

cleanup:
  if (target_verified != NULL) {
    *target_verified = verified;
  }

  xSemaphoreGive(s_usb_mux_state.mutex);
  return ret;
}