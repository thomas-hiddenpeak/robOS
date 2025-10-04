/**
 * @file device_controller.c
 * @brief Device Controller Component Implementation
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#include "device_controller.h"
#include "config_manager.h"
#include "esp_log.h"
#include "gpio_controller.h"
#include "usb_mux_controller.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

/* ============================================================================
 * Private Constants
 * ============================================================================
 */

#define DEVICE_CONFIG_NAMESPACE "device"
#define DEVICE_CONFIG_KEY_AUTO_START_LPMU "lpmu_auto"

/* ============================================================================
 * Private Variables
 * ============================================================================
 */

static device_status_t s_device_status = {0};
static device_config_t s_device_config = {0};
static const char *TAG = DEVICE_CONTROLLER_TAG;
static SemaphoreHandle_t s_device_mutex = NULL;

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

static esp_err_t init_device_gpio_pins(void);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

esp_err_t device_controller_init(void) {
  if (s_device_status.initialized) {
    ESP_LOGW(TAG, "Device controller already initialized");
    return ESP_OK;
  }

  // Check dependencies
  if (!gpio_controller_is_initialized()) {
    ESP_LOGE(TAG, "GPIO controller is not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Create mutex
  s_device_mutex = xSemaphoreCreateMutex();
  if (s_device_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }

  // Initialize GPIO pins
  esp_err_t ret = init_device_gpio_pins();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize device GPIO pins");
    vSemaphoreDelete(s_device_mutex);
    s_device_mutex = NULL;
    return ret;
  }

  // Initialize status (configuration will be loaded later after config_manager
  // is ready)
  s_device_status.initialized = true;
  s_device_status.agx_power_state =
      POWER_STATE_ON; // AGX defaults to ON (GPIO3=LOW)
  s_device_status.lpmu_power_state =
      POWER_STATE_OFF; // Initial state, will be updated after config load
  s_device_status.agx_operations_count = 0;
  s_device_status.lpmu_operations_count = 0;

  // Set default configuration (will be updated later)
  s_device_config = device_controller_get_default_config();

  ESP_LOGI(TAG, "Device controller initialized successfully");
  ESP_LOGI(TAG, "AGX - Power: GPIO%d, Reset: GPIO%d, Recovery: GPIO%d",
           AGX_POWER_PIN, AGX_RESET_PIN, AGX_RECOVERY_PIN);
  ESP_LOGI(TAG, "LPMU - Power: GPIO%d, Reset: GPIO%d", LPMU_POWER_BTN_PIN,
           LPMU_RESET_PIN);

  // Note: LPMU auto-start will be handled later in main.c after config_manager
  // is ready

  return ESP_OK;
}

esp_err_t device_controller_deinit(void) {
  if (!s_device_status.initialized) {
    ESP_LOGW(TAG, "Device controller not initialized");
    return ESP_OK;
  }

  // Take mutex before cleanup
  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    s_device_status.initialized = false;
    xSemaphoreGive(s_device_mutex);
  }

  // Delete mutex
  if (s_device_mutex != NULL) {
    vSemaphoreDelete(s_device_mutex);
    s_device_mutex = NULL;
  }

  ESP_LOGI(TAG, "Device controller deinitialized");
  return ESP_OK;
}

bool device_controller_is_initialized(void) {
  return s_device_status.initialized;
}

// ==================== AGX Device Control ====================

esp_err_t device_controller_agx_power_on(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Powering on AGX device");

  // AGX power on: GPIO3 = LOW
  esp_err_t ret = gpio_controller_set_output(AGX_POWER_PIN, GPIO_STATE_LOW);
  if (ret == ESP_OK) {
    s_device_status.agx_power_state = POWER_STATE_ON;
    s_device_status.agx_operations_count++;
    ESP_LOGI(TAG, "AGX powered on (GPIO%d set to LOW)", AGX_POWER_PIN);
  } else {
    ESP_LOGE(TAG, "Failed to power on AGX: %s", esp_err_to_name(ret));
  }

  xSemaphoreGive(s_device_mutex);
  return ret;
}

esp_err_t device_controller_agx_power_off(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Powering off AGX device");

  // AGX power off: GPIO3 = HIGH
  esp_err_t ret = gpio_controller_set_output(AGX_POWER_PIN, GPIO_STATE_HIGH);
  if (ret == ESP_OK) {
    s_device_status.agx_power_state = POWER_STATE_OFF;
    s_device_status.agx_operations_count++;
    ESP_LOGI(TAG, "AGX powered off (GPIO%d set to HIGH)", AGX_POWER_PIN);
  } else {
    ESP_LOGE(TAG, "Failed to power off AGX: %s", esp_err_to_name(ret));
  }

  xSemaphoreGive(s_device_mutex);
  return ret;
}

esp_err_t device_controller_agx_reset(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Resetting AGX device");

  // Step 1: Pull reset pin high
  esp_err_t ret = gpio_controller_set_output(AGX_RESET_PIN, GPIO_STATE_HIGH);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AGX reset pin high: %s", esp_err_to_name(ret));
    xSemaphoreGive(s_device_mutex);
    return ret;
  }

  // Step 2: Hold for reset pulse duration
  vTaskDelay(pdMS_TO_TICKS(AGX_RESET_PULSE_MS));

  // Step 3: Pull reset pin low
  ret = gpio_controller_set_output(AGX_RESET_PIN, GPIO_STATE_LOW);
  if (ret == ESP_OK) {
    s_device_status.agx_operations_count++;
    ESP_LOGI(TAG, "AGX reset completed");
  } else {
    ESP_LOGE(TAG, "Failed to set AGX reset pin low: %s", esp_err_to_name(ret));
  }

  xSemaphoreGive(s_device_mutex);
  return ret;
}

esp_err_t device_controller_agx_enter_recovery_mode(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Entering AGX recovery mode");

  // Step 1: Pull recovery pin high
  esp_err_t ret = gpio_controller_set_output(AGX_RECOVERY_PIN, GPIO_STATE_HIGH);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AGX recovery pin high: %s",
             esp_err_to_name(ret));
    xSemaphoreGive(s_device_mutex);
    return ret;
  }

  // Step 2: Hold for 1 second
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Step 3: Pull recovery pin low
  ret = gpio_controller_set_output(AGX_RECOVERY_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AGX recovery pin low: %s",
             esp_err_to_name(ret));
    xSemaphoreGive(s_device_mutex);
    return ret;
  }

  xSemaphoreGive(s_device_mutex);

  // Step 4: Switch USB MUX to AGX (outside mutex to avoid deadlock)
  if (usb_mux_controller_is_initialized()) {
    ESP_LOGI(TAG, "Switching USB MUX to AGX for recovery");
    ret = usb_mux_controller_set_target(USB_MUX_TARGET_AGX);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to switch USB MUX to AGX during recovery mode");
      return ret;
    }
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    s_device_status.agx_operations_count++;
    xSemaphoreGive(s_device_mutex);
  }

  ESP_LOGI(TAG, "AGX recovery mode entry completed successfully");
  return ESP_OK;
}

esp_err_t device_controller_agx_get_power_state(power_state_t *state) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (state == NULL) {
    ESP_LOGE(TAG, "State pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  *state = s_device_status.agx_power_state;

  xSemaphoreGive(s_device_mutex);
  return ESP_OK;
}

// ==================== LPMU Device Control ====================

esp_err_t device_controller_lpmu_power_toggle(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Toggling LPMU power");

  // Step 1: Pull power button pin high
  esp_err_t ret =
      gpio_controller_set_output(LPMU_POWER_BTN_PIN, GPIO_STATE_HIGH);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set LPMU power button high: %s",
             esp_err_to_name(ret));
    xSemaphoreGive(s_device_mutex);
    return ret;
  }

  // Step 2: Hold for power pulse duration
  vTaskDelay(pdMS_TO_TICKS(LPMU_POWER_PULSE_MS));

  // Step 3: Pull power button pin low
  ret = gpio_controller_set_output(LPMU_POWER_BTN_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set LPMU power button low: %s",
             esp_err_to_name(ret));
    xSemaphoreGive(s_device_mutex);
    return ret;
  }

  // Update power state (toggle between ON/OFF)
  // Special handling for UNKNOWN state - assume first toggle turns ON
  if (s_device_status.lpmu_power_state == POWER_STATE_UNKNOWN) {
    s_device_status.lpmu_power_state = POWER_STATE_ON;
    ESP_LOGI(TAG, "LPMU power toggled from UNKNOWN to ON (first boot)");
  } else if (s_device_status.lpmu_power_state == POWER_STATE_ON) {
    s_device_status.lpmu_power_state = POWER_STATE_OFF;
    ESP_LOGI(TAG, "LPMU power toggled to OFF");
  } else {
    s_device_status.lpmu_power_state = POWER_STATE_ON;
    ESP_LOGI(TAG, "LPMU power toggled to ON");
  }

  s_device_status.lpmu_operations_count++;

  xSemaphoreGive(s_device_mutex);
  return ESP_OK;
}

esp_err_t device_controller_lpmu_reset(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Resetting LPMU device");

  // Step 1: Pull reset pin high
  esp_err_t ret = gpio_controller_set_output(LPMU_RESET_PIN, GPIO_STATE_HIGH);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set LPMU reset pin high: %s",
             esp_err_to_name(ret));
    xSemaphoreGive(s_device_mutex);
    return ret;
  }

  // Step 2: Hold for reset pulse duration
  vTaskDelay(pdMS_TO_TICKS(LPMU_RESET_PULSE_MS));

  // Step 3: Pull reset pin low
  ret = gpio_controller_set_output(LPMU_RESET_PIN, GPIO_STATE_LOW);
  if (ret == ESP_OK) {
    s_device_status.lpmu_operations_count++;
    ESP_LOGI(TAG, "LPMU reset completed");
  } else {
    ESP_LOGE(TAG, "Failed to set LPMU reset pin low: %s", esp_err_to_name(ret));
  }

  xSemaphoreGive(s_device_mutex);
  return ret;
}

esp_err_t device_controller_lpmu_get_power_state(power_state_t *state) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (state == NULL) {
    ESP_LOGE(TAG, "State pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  *state = s_device_status.lpmu_power_state;

  xSemaphoreGive(s_device_mutex);
  return ESP_OK;
}

// ==================== Utility Functions ====================

const char *device_controller_get_power_state_name(power_state_t state) {
  switch (state) {
  case POWER_STATE_OFF:
    return "关闭";
  case POWER_STATE_ON:
    return "开启";
  case POWER_STATE_UNKNOWN:
    return "未知";
  default:
    return "无效";
  }
}

esp_err_t device_controller_get_status(device_status_t *status) {
  if (!s_device_status.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  memcpy(status, &s_device_status, sizeof(device_status_t));

  xSemaphoreGive(s_device_mutex);
  return ESP_OK;
}

esp_err_t device_controller_test_agx_power(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Starting AGX power control test");

  // Test power on
  ESP_LOGI(TAG, "Testing AGX power on");
  esp_err_t ret = device_controller_agx_power_on();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AGX power on test failed");
    return ESP_FAIL;
  }
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Test power off
  ESP_LOGI(TAG, "Testing AGX power off");
  ret = device_controller_agx_power_off();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AGX power off test failed");
    return ESP_FAIL;
  }
  vTaskDelay(pdMS_TO_TICKS(2000));

  ESP_LOGI(TAG, "AGX power control test completed successfully");
  return ESP_OK;
}

esp_err_t device_controller_test_lpmu_power(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Starting LPMU power control test");

  // Test power toggle
  ESP_LOGI(TAG, "Testing LPMU power toggle");
  esp_err_t ret = device_controller_lpmu_power_toggle();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LPMU power toggle test failed");
    return ESP_FAIL;
  }
  vTaskDelay(pdMS_TO_TICKS(3000));

  // Test toggle again
  ESP_LOGI(TAG, "Testing LPMU power toggle again");
  ret = device_controller_lpmu_power_toggle();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LPMU power toggle test failed");
    return ESP_FAIL;
  }
  vTaskDelay(pdMS_TO_TICKS(3000));

  ESP_LOGI(TAG, "LPMU power control test completed successfully");
  return ESP_OK;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static esp_err_t init_device_gpio_pins(void) {
  esp_err_t ret = ESP_OK;

  // Initialize AGX power pin (default: LOW for ON state)
  ret = gpio_controller_set_output(AGX_POWER_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize AGX power pin: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Initialize AGX reset pin (default: LOW)
  ret = gpio_controller_set_output(AGX_RESET_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize AGX reset pin: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Initialize AGX recovery pin (default: LOW)
  ret = gpio_controller_set_output(AGX_RECOVERY_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize AGX recovery pin: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Initialize LPMU power button pin (default: LOW)
  ret = gpio_controller_set_output(LPMU_POWER_BTN_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LPMU power button pin: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Initialize LPMU reset pin (default: LOW)
  ret = gpio_controller_set_output(LPMU_RESET_PIN, GPIO_STATE_LOW);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LPMU reset pin: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Device GPIO pins initialized successfully");
  return ESP_OK;
}

/* ============================================================================
 * Configuration Management Functions
 * ============================================================================
 */

device_config_t device_controller_get_default_config(void) {
  device_config_t config = {
      .auto_start_lpmu = true // Default: auto-start LPMU on boot
  };
  return config;
}

esp_err_t device_controller_load_config(device_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "Config pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Start with default values
  *config = device_controller_get_default_config();

  // Load auto_start_lpmu setting
  bool auto_start;
  ESP_LOGI(TAG, "Attempting to load config: namespace='%s', key='%s'",
           DEVICE_CONFIG_NAMESPACE, DEVICE_CONFIG_KEY_AUTO_START_LPMU);

  // Try direct call to config_manager_get instead of macro
  esp_err_t ret = config_manager_get(DEVICE_CONFIG_NAMESPACE,
                                     DEVICE_CONFIG_KEY_AUTO_START_LPMU,
                                     CONFIG_TYPE_BOOL, &auto_start, NULL);
  if (ret == ESP_OK) {
    config->auto_start_lpmu = auto_start;
    ESP_LOGI(TAG, "Successfully loaded auto_start_lpmu: %s",
             auto_start ? "true" : "false");
  } else if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGI(TAG, "auto_start_lpmu not configured, using default: true");
    // This is not an error, just use the default value
  } else {
    ESP_LOGW(TAG,
             "Failed to load auto_start_lpmu (namespace='%s', key='%s'): %s",
             DEVICE_CONFIG_NAMESPACE, DEVICE_CONFIG_KEY_AUTO_START_LPMU,
             esp_err_to_name(ret));
    // Don't return error, just use default values
    ESP_LOGI(TAG, "Using default configuration");
  }

  return ESP_OK;
}

esp_err_t device_controller_save_config(const device_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "Config pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Save auto_start_lpmu setting
  bool auto_start_value = config->auto_start_lpmu;
  ESP_LOGI(TAG, "Attempting to save config: namespace='%s', key='%s', value=%s",
           DEVICE_CONFIG_NAMESPACE, DEVICE_CONFIG_KEY_AUTO_START_LPMU,
           auto_start_value ? "true" : "false");

  // Try direct call to config_manager_set instead of macro
  esp_err_t ret = config_manager_set(
      DEVICE_CONFIG_NAMESPACE, DEVICE_CONFIG_KEY_AUTO_START_LPMU,
      CONFIG_TYPE_BOOL, &auto_start_value, sizeof(bool));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save auto_start_lpmu: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Device configuration saved successfully");
  return ESP_OK;
}

esp_err_t device_controller_set_lpmu_auto_start(bool auto_start) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  // Update configuration
  s_device_config.auto_start_lpmu = auto_start;

  // Save to NVS
  esp_err_t ret = device_controller_save_config(&s_device_config);

  xSemaphoreGive(s_device_mutex);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "LPMU auto-start %s", auto_start ? "enabled" : "disabled");
  }

  return ret;
}

esp_err_t device_controller_get_lpmu_auto_start(bool *auto_start) {
  if (auto_start == NULL) {
    ESP_LOGE(TAG, "auto_start pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_device_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  *auto_start = s_device_config.auto_start_lpmu;

  xSemaphoreGive(s_device_mutex);
  return ESP_OK;
}

esp_err_t device_controller_post_config_init(void) {
  if (!s_device_status.initialized) {
    ESP_LOGE(TAG, "Device controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Loading device configuration and handling LPMU auto-start...");

  // Load device configuration now that config_manager is ready
  esp_err_t ret = device_controller_load_config(&s_device_config);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load configuration, using defaults");
    s_device_config = device_controller_get_default_config();
  }

  ESP_LOGI(TAG, "Configuration loaded: LPMU auto-start = %s",
           s_device_config.auto_start_lpmu ? "enabled" : "disabled");

  // Handle LPMU auto-start if configured
  if (s_device_config.auto_start_lpmu) {
    ESP_LOGI(TAG, "Auto-starting LPMU...");
    ret = device_controller_lpmu_power_toggle();
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "LPMU auto-start completed successfully, state: %s",
               device_controller_get_power_state_name(
                   s_device_status.lpmu_power_state));
    } else {
      ESP_LOGW(TAG, "LPMU auto-start failed: %s", esp_err_to_name(ret));
      // If auto-start failed, set state to OFF
      s_device_status.lpmu_power_state = POWER_STATE_OFF;
    }
  } else {
    ESP_LOGI(TAG, "LPMU auto-start disabled");
  }

  return ESP_OK;
}