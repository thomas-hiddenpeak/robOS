/**
 * @file gpio_controller.c
 * @brief GPIO Controller Component Implementation
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#include "gpio_controller.h"
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

/* ============================================================================
 * Private Constants and Macros
 * ============================================================================
 */

#define GPIO_CONFIG_ARRAY_SIZE                                                 \
  (GPIO_MAX_PIN_NUM + 1) ///< Size of pin config array

/* ============================================================================
 * Private Type Definitions
 * ============================================================================
 */

/**
 * @brief GPIO controller internal state
 */
typedef struct {
  bool initialized; ///< Initialization status
  gpio_pin_config_t pin_configs[GPIO_CONFIG_ARRAY_SIZE]; ///< Pin configurations
  uint32_t total_operations; ///< Total operations counter
  SemaphoreHandle_t mutex;   ///< Mutex for thread safety
} gpio_controller_state_t;

/* ============================================================================
 * Private Variables
 * ============================================================================
 */

static gpio_controller_state_t s_gpio_state = {0};
static const char *TAG = GPIO_CONTROLLER_TAG;

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

static bool is_valid_gpio_pin(uint8_t pin);
static esp_err_t configure_gpio_pin(uint8_t pin, gpio_mode_t mode);
static esp_err_t update_pin_config(uint8_t pin, gpio_ctrl_mode_t mode,
                                   gpio_state_t state);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

esp_err_t gpio_controller_init(void) {
  if (s_gpio_state.initialized) {
    ESP_LOGW(TAG, "GPIO controller already initialized");
    return ESP_OK;
  }

  // Initialize mutex
  s_gpio_state.mutex = xSemaphoreCreateMutex();
  if (s_gpio_state.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }

  // Clear pin configurations
  memset(s_gpio_state.pin_configs, 0, sizeof(s_gpio_state.pin_configs));
  s_gpio_state.total_operations = 0;
  s_gpio_state.initialized = true;

  ESP_LOGI(TAG, "GPIO controller initialized successfully");
  return ESP_OK;
}

esp_err_t gpio_controller_deinit(void) {
  if (!s_gpio_state.initialized) {
    ESP_LOGW(TAG, "GPIO controller not initialized");
    return ESP_OK;
  }

  // Take mutex before cleanup
  if (xSemaphoreTake(s_gpio_state.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // Reset all configured pins
    for (int i = 0; i <= GPIO_MAX_PIN_NUM; i++) {
      if (s_gpio_state.pin_configs[i].configured) {
        gpio_reset_pin(i);
        s_gpio_state.pin_configs[i].configured = false;
      }
    }

    s_gpio_state.initialized = false;
    xSemaphoreGive(s_gpio_state.mutex);
  }

  // Delete mutex
  if (s_gpio_state.mutex != NULL) {
    vSemaphoreDelete(s_gpio_state.mutex);
    s_gpio_state.mutex = NULL;
  }

  ESP_LOGI(TAG, "GPIO controller deinitialized");
  return ESP_OK;
}

bool gpio_controller_is_initialized(void) { return s_gpio_state.initialized; }

esp_err_t gpio_controller_set_output(uint8_t pin, gpio_state_t state) {
  if (!s_gpio_state.initialized) {
    ESP_LOGE(TAG, "GPIO controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!is_valid_gpio_pin(pin)) {
    ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
    return ESP_ERR_INVALID_ARG;
  }

  if (state != GPIO_STATE_LOW && state != GPIO_STATE_HIGH) {
    ESP_LOGE(TAG, "Invalid GPIO state: %d", state);
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_gpio_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;

  // Configure pin as output if not already configured
  gpio_pin_config_t *config = &s_gpio_state.pin_configs[pin];
  if (!config->configured || config->mode != GPIO_CTRL_MODE_OUTPUT) {
    ret = configure_gpio_pin(pin, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to configure GPIO%d as output: %s", pin,
               esp_err_to_name(ret));
      goto cleanup;
    }
  }

  // Set GPIO level
  ret = gpio_set_level(pin, (uint32_t)state);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set GPIO%d level: %s", pin, esp_err_to_name(ret));
    goto cleanup;
  }

  // Update internal configuration
  ret = update_pin_config(pin, GPIO_CTRL_MODE_OUTPUT, state);
  if (ret == ESP_OK) {
    s_gpio_state.total_operations++;
    ESP_LOGD(TAG, "GPIO%d set to %s", pin,
             state == GPIO_STATE_HIGH ? "HIGH" : "LOW");
  }

cleanup:
  xSemaphoreGive(s_gpio_state.mutex);
  return ret;
}

esp_err_t gpio_controller_read_input(uint8_t pin, gpio_state_t *state) {
  if (!s_gpio_state.initialized) {
    ESP_LOGE(TAG, "GPIO controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!is_valid_gpio_pin(pin)) {
    ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
    return ESP_ERR_INVALID_ARG;
  }

  if (state == NULL) {
    ESP_LOGE(TAG, "State pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_gpio_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;

  // Configure pin as input
  ret = configure_gpio_pin(pin, GPIO_MODE_INPUT);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure GPIO%d as input: %s", pin,
             esp_err_to_name(ret));
    goto cleanup;
  }

  // Read GPIO level
  int level = gpio_get_level(pin);
  *state = (level == 1) ? GPIO_STATE_HIGH : GPIO_STATE_LOW;

  // Update internal configuration
  ret = update_pin_config(pin, GPIO_CTRL_MODE_INPUT, *state);
  if (ret == ESP_OK) {
    s_gpio_state.total_operations++;
    ESP_LOGD(TAG, "GPIO%d read as %s", pin,
             *state == GPIO_STATE_HIGH ? "HIGH" : "LOW");
  }

cleanup:
  xSemaphoreGive(s_gpio_state.mutex);
  return ret;
}

esp_err_t gpio_controller_get_pin_config(uint8_t pin,
                                         gpio_pin_config_t *config) {
  if (!s_gpio_state.initialized) {
    ESP_LOGE(TAG, "GPIO controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!is_valid_gpio_pin(pin)) {
    ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
    return ESP_ERR_INVALID_ARG;
  }

  if (config == NULL) {
    ESP_LOGE(TAG, "Config pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_gpio_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  gpio_pin_config_t *internal_config = &s_gpio_state.pin_configs[pin];
  if (!internal_config->configured) {
    xSemaphoreGive(s_gpio_state.mutex);
    return ESP_ERR_NOT_FOUND;
  }

  // Copy configuration
  memcpy(config, internal_config, sizeof(gpio_pin_config_t));

  xSemaphoreGive(s_gpio_state.mutex);
  return ESP_OK;
}

esp_err_t gpio_controller_reset_pin(uint8_t pin) {
  if (!s_gpio_state.initialized) {
    ESP_LOGE(TAG, "GPIO controller not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!is_valid_gpio_pin(pin)) {
    ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_gpio_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  esp_err_t ret = gpio_reset_pin(pin);
  if (ret == ESP_OK) {
    // Clear internal configuration
    memset(&s_gpio_state.pin_configs[pin], 0, sizeof(gpio_pin_config_t));
    s_gpio_state.total_operations++;
    ESP_LOGD(TAG, "GPIO%d reset to default state", pin);
  } else {
    ESP_LOGE(TAG, "Failed to reset GPIO%d: %s", pin, esp_err_to_name(ret));
  }

  xSemaphoreGive(s_gpio_state.mutex);
  return ret;
}

esp_err_t gpio_controller_validate_pin(uint8_t pin) {
  return is_valid_gpio_pin(pin) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t gpio_controller_get_status(uint32_t *configured_pins_count,
                                     uint32_t *total_operations) {
  if (!s_gpio_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_gpio_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex");
    return ESP_FAIL;
  }

  if (configured_pins_count != NULL) {
    uint32_t count = 0;
    for (int i = 0; i <= GPIO_MAX_PIN_NUM; i++) {
      if (s_gpio_state.pin_configs[i].configured) {
        count++;
      }
    }
    *configured_pins_count = count;
  }

  if (total_operations != NULL) {
    *total_operations = s_gpio_state.total_operations;
  }

  xSemaphoreGive(s_gpio_state.mutex);
  return ESP_OK;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static bool is_valid_gpio_pin(uint8_t pin) {
  // ESP32S3 valid GPIO pins (excluding strapping pins and special pins)
  // Valid pins: 0-21, 26, 33-48
  if (pin <= 21) {
    return true;
  }
  if (pin == 26) {
    return true;
  }
  if (pin >= 33 && pin <= 48) {
    return true;
  }
  return false;
}

static esp_err_t configure_gpio_pin(uint8_t pin, gpio_mode_t mode) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << pin),
      .mode = mode,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", pin, esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t update_pin_config(uint8_t pin, gpio_ctrl_mode_t mode,
                                   gpio_state_t state) {
  gpio_pin_config_t *config = &s_gpio_state.pin_configs[pin];

  config->pin = pin;
  config->mode = mode;
  config->state = state;
  config->configured = true;

  return ESP_OK;
}
