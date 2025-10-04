/**
 * @file agx_monitor.c
 * @brief AGX Monitor Component Implementation
 *
 * This file implements the core AGX monitoring functionality for robOS,
 * including WebSocket client management, data parsing, and connection handling.
 *
 * @version 1.0.0
 * @date 2025-10-04
 * @author robOS Team
 */

#include "agx_monitor.h"
#include "config_manager.h"
#include "console_core.h"
#include "event_manager.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "agx_monitor";

/* ============================================================================
 * Internal State Management
 * ============================================================================
 */

/**
 * @brief Internal AGX monitor state structure
 */
typedef struct {
  bool initialized;                       ///< Initialization flag
  bool running;                           ///< Running flag
  agx_monitor_config_t config;            ///< Current configuration
  agx_monitor_status_t connection_status; ///< Connection status

  // WebSocket client
  esp_websocket_client_handle_t ws_client; ///< WebSocket client handle

  // Data storage
  agx_monitor_data_t latest_data; ///< Latest monitoring data
  SemaphoreHandle_t data_mutex;   ///< Data access mutex

  // Task management
  TaskHandle_t monitor_task_handle;   ///< Monitor task handle
  TaskHandle_t reconnect_task_handle; ///< Reconnect task handle

  // Statistics and error tracking
  uint32_t total_reconnects;     ///< Total reconnection attempts
  uint32_t messages_received;    ///< Messages received counter
  uint32_t parse_errors;         ///< Parse errors counter
  uint64_t last_message_time_us; ///< Last message timestamp
  uint64_t start_time_us;        ///< Component start time
  uint64_t connected_time_us;    ///< Total connected time
  char last_error[AGX_MONITOR_MAX_ERROR_MSG_LENGTH]; ///< Last error message

  // Event callback
  agx_monitor_event_callback_t event_callback; ///< Event callback function
  void *callback_user_data;                    ///< Event callback user data

} agx_monitor_state_t;

static agx_monitor_state_t s_agx_monitor = {0};

/* ============================================================================
 * Forward Declarations
 * ============================================================================
 */

// Core functionality
static esp_err_t agx_monitor_websocket_init(void);
static esp_err_t agx_monitor_websocket_deinit(void);
static esp_err_t agx_monitor_connect(void);
static esp_err_t agx_monitor_disconnect(void);

// Task functions
static void agx_monitor_task(void *pvParameters);
static void agx_monitor_reconnect_task(void *pvParameters);

// WebSocket event handlers
static void agx_monitor_websocket_event_handler(void *handler_args,
                                                esp_event_base_t base,
                                                int32_t event_id,
                                                void *event_data);

// Data processing
static esp_err_t agx_monitor_parse_data(const char *json_data, size_t data_len);
static esp_err_t agx_monitor_parse_cpu_data(cJSON *cpu_json,
                                            agx_monitor_data_t *data);
static esp_err_t agx_monitor_parse_memory_data(cJSON *memory_json,
                                               agx_monitor_data_t *data);
static esp_err_t agx_monitor_parse_temperature_data(cJSON *temp_json,
                                                    agx_monitor_data_t *data);
static esp_err_t agx_monitor_parse_power_data(cJSON *power_json,
                                              agx_monitor_data_t *data);
static esp_err_t agx_monitor_parse_gpu_data(cJSON *gpu_json,
                                            agx_monitor_data_t *data);

// Utility functions
static void agx_monitor_update_status(agx_monitor_status_t new_status);
static void agx_monitor_trigger_event(agx_monitor_event_type_t event_type,
                                      void *event_data);
static void agx_monitor_update_statistics(void);
static void agx_monitor_set_error(const char *error_msg);

// Console command handlers
static esp_err_t cmd_agx_status(int argc, char **argv);
static esp_err_t cmd_agx_start(int argc, char **argv);
static esp_err_t cmd_agx_stop(int argc, char **argv);
static esp_err_t cmd_agx_data(int argc, char **argv);
static esp_err_t cmd_agx_config(int argc, char **argv);
static esp_err_t cmd_agx_stats(int argc, char **argv);
static esp_err_t cmd_agx_debug(int argc, char **argv);

// Console command registration
static esp_err_t agx_monitor_register_commands(void);
static esp_err_t agx_monitor_unregister_commands(void);

/* ============================================================================
 * Public API Implementation
 * ============================================================================
 */

esp_err_t agx_monitor_get_default_config(agx_monitor_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "Configuration pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  memset(config, 0, sizeof(agx_monitor_config_t));

  // Set default values according to specification
  strncpy(config->server_url, AGX_MONITOR_DEFAULT_SERVER_URL,
          AGX_MONITOR_MAX_URL_LENGTH - 1);
  config->server_port = AGX_MONITOR_DEFAULT_SERVER_PORT;
  config->reconnect_interval_ms = AGX_MONITOR_DEFAULT_RECONNECT_INTERVAL_MS;
  config->fast_retry_count = AGX_MONITOR_DEFAULT_FAST_RETRY_COUNT;
  config->fast_retry_interval_ms = AGX_MONITOR_DEFAULT_FAST_RETRY_INTERVAL_MS;
  config->heartbeat_timeout_ms = AGX_MONITOR_DEFAULT_HEARTBEAT_TIMEOUT_MS;
  config->enable_ssl = false;
  config->auto_start = true;
  config->startup_delay_ms = AGX_MONITOR_DEFAULT_STARTUP_DELAY_MS;
  config->task_stack_size = AGX_MONITOR_DEFAULT_TASK_STACK_SIZE;
  config->task_priority = AGX_MONITOR_DEFAULT_TASK_PRIORITY;

  ESP_LOGD(TAG, "Default configuration created");
  return ESP_OK;
}

esp_err_t agx_monitor_init(const agx_monitor_config_t *config) {
  if (s_agx_monitor.initialized) {
    ESP_LOGW(TAG, "AGX monitor already initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (config == NULL) {
    ESP_LOGE(TAG, "Configuration is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Validate configuration parameters
  if (strlen(config->server_url) == 0) {
    ESP_LOGE(TAG, "Server URL cannot be empty");
    return ESP_ERR_INVALID_ARG;
  }

  if (config->server_port == 0 || config->server_port > 65535) {
    ESP_LOGE(TAG, "Invalid server port: %d", config->server_port);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->task_stack_size < 4096) {
    ESP_LOGE(TAG, "Task stack size too small: %lu (minimum: 4096)",
             config->task_stack_size);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->task_priority > configMAX_PRIORITIES - 1) {
    ESP_LOGE(TAG, "Task priority too high: %d (maximum: %d)",
             config->task_priority, configMAX_PRIORITIES - 1);
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Initializing AGX monitor v%s", AGX_MONITOR_VERSION);

  // Completely silence ESP-IDF WebSocket library logs to prevent console
  // interference
  esp_log_level_set("websocket_client", ESP_LOG_NONE);
  esp_log_level_set("transport_ws", ESP_LOG_NONE);
  esp_log_level_set("transport", ESP_LOG_NONE);

  // Clear the state structure
  memset(&s_agx_monitor, 0, sizeof(agx_monitor_state_t));

  // Copy configuration
  memcpy(&s_agx_monitor.config, config, sizeof(agx_monitor_config_t));

  // Create data mutex
  s_agx_monitor.data_mutex = xSemaphoreCreateMutex();
  if (s_agx_monitor.data_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create data mutex");
    return ESP_ERR_NO_MEM;
  }

  // Initialize data structure
  memset(&s_agx_monitor.latest_data, 0, sizeof(agx_monitor_data_t));
  s_agx_monitor.latest_data.is_valid = false;

  // Initialize status and timing
  s_agx_monitor.connection_status = AGX_MONITOR_STATUS_INITIALIZED;
  s_agx_monitor.running = false;
  s_agx_monitor.start_time_us = esp_timer_get_time();

  // Reset statistics
  s_agx_monitor.total_reconnects = 0;
  s_agx_monitor.messages_received = 0;
  s_agx_monitor.parse_errors = 0;
  s_agx_monitor.last_message_time_us = 0;
  s_agx_monitor.connected_time_us = 0;
  memset(s_agx_monitor.last_error, 0, sizeof(s_agx_monitor.last_error));

  // Initialize task handles to NULL
  s_agx_monitor.monitor_task_handle = NULL;
  s_agx_monitor.reconnect_task_handle = NULL;
  s_agx_monitor.ws_client = NULL;
  s_agx_monitor.event_callback = NULL;
  s_agx_monitor.callback_user_data = NULL;

  // Initialize WebSocket client (placeholder for now)
  esp_err_t ret = agx_monitor_websocket_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WebSocket client: %s",
             esp_err_to_name(ret));
    vSemaphoreDelete(s_agx_monitor.data_mutex);
    s_agx_monitor.data_mutex = NULL;
    return ret;
  }

  s_agx_monitor.initialized = true;

  // Register console commands
  ret = agx_monitor_register_commands();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register console commands: %s",
             esp_err_to_name(ret));
    // Continue initialization even if command registration fails
  }

  ESP_LOGI(TAG, "AGX monitor initialized successfully");
  ESP_LOGD(TAG, "Server: %s:%d", s_agx_monitor.config.server_url,
           s_agx_monitor.config.server_port);
  ESP_LOGD(TAG, "Reconnect interval: %lu ms",
           s_agx_monitor.config.reconnect_interval_ms);
  ESP_LOGD(TAG, "Fast retry: %d attempts, %lu ms interval",
           s_agx_monitor.config.fast_retry_count,
           s_agx_monitor.config.fast_retry_interval_ms);
  ESP_LOGD(TAG, "Task configuration: stack=%lu, priority=%d",
           s_agx_monitor.config.task_stack_size,
           s_agx_monitor.config.task_priority);
  if (s_agx_monitor.config.startup_delay_ms > 0) {
    ESP_LOGD(TAG, "AGX Startup delay: %lu ms (%.1f seconds)",
             s_agx_monitor.config.startup_delay_ms,
             s_agx_monitor.config.startup_delay_ms / 1000.0f);
  }

  // Auto-start if configured
  if (s_agx_monitor.config.auto_start) {
    ESP_LOGD(TAG, "Auto-starting AGX monitor");
    return agx_monitor_start();
  }

  return ESP_OK;
}

esp_err_t agx_monitor_deinit(void) {
  if (!s_agx_monitor.initialized) {
    ESP_LOGW(TAG, "AGX monitor not initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Deinitializing AGX monitor");

  // Stop monitoring if running
  if (s_agx_monitor.running) {
    esp_err_t ret = agx_monitor_stop();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Error stopping monitor: %s", esp_err_to_name(ret));
    }
  }

  // Give tasks time to finish if they were running
  vTaskDelay(pdMS_TO_TICKS(100));

  // Clean up WebSocket resources
  esp_err_t ret = agx_monitor_websocket_deinit();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Error deinitializing WebSocket: %s", esp_err_to_name(ret));
  }

  // Delete mutex and synchronize
  if (s_agx_monitor.data_mutex) {
    // Ensure no one is waiting on mutex
    if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(1000))) {
      xSemaphoreGive(s_agx_monitor.data_mutex);
    }
    vSemaphoreDelete(s_agx_monitor.data_mutex);
    s_agx_monitor.data_mutex = NULL;
  }

  // Unregister console commands
  agx_monitor_unregister_commands();

  // Clear event callback
  s_agx_monitor.event_callback = NULL;
  s_agx_monitor.callback_user_data = NULL;

  // Reset state completely
  memset(&s_agx_monitor, 0, sizeof(agx_monitor_state_t));

  ESP_LOGI(TAG, "AGX monitor deinitialized successfully");
  return ESP_OK;
}

esp_err_t agx_monitor_start(void) {
  if (!s_agx_monitor.initialized) {
    ESP_LOGE(TAG, "AGX monitor not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (s_agx_monitor.running) {
    ESP_LOGW(TAG, "AGX monitor already running");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGD(TAG, "Starting AGX monitor");

  // Update status to indicate we're starting
  agx_monitor_update_status(AGX_MONITOR_STATUS_CONNECTING);

  // Reset runtime statistics
  s_agx_monitor.running = true;
  s_agx_monitor.start_time_us = esp_timer_get_time();

  // Invalidate any old data
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(1000))) {
    s_agx_monitor.latest_data.is_valid = false;
    s_agx_monitor.latest_data.update_time_us = 0;
    xSemaphoreGive(s_agx_monitor.data_mutex);
  } else {
    ESP_LOGW(TAG, "Failed to acquire mutex during start");
  }

  // Create monitoring task
  BaseType_t ret = xTaskCreate(
      agx_monitor_task, "agx_monitor_task",
      s_agx_monitor.config.task_stack_size / sizeof(StackType_t), NULL,
      s_agx_monitor.config.task_priority, &s_agx_monitor.monitor_task_handle);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create monitor task: %d", ret);
    s_agx_monitor.running = false;
    agx_monitor_update_status(AGX_MONITOR_STATUS_ERROR);
    agx_monitor_set_error("Failed to create monitor task");
    return ESP_ERR_NO_MEM;
  }

  // Trigger connected event
  agx_monitor_trigger_event(AGX_MONITOR_EVENT_CONNECTED, NULL);

  ESP_LOGD(TAG, "AGX monitor started successfully");
  ESP_LOGD(TAG, "Monitor task created with stack size: %lu bytes",
           s_agx_monitor.config.task_stack_size);

  return ESP_OK;
}

esp_err_t agx_monitor_stop(void) {
  if (!s_agx_monitor.initialized) {
    ESP_LOGE(TAG, "AGX monitor not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_agx_monitor.running) {
    ESP_LOGW(TAG, "AGX monitor not running");
    return ESP_OK;
  }

  ESP_LOGD(TAG, "Stopping AGX monitor");

  // Set running flag to false first to signal tasks to stop
  s_agx_monitor.running = false;

  // Disconnect WebSocket to stop data reception
  esp_err_t ret = agx_monitor_disconnect();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Error disconnecting WebSocket: %s", esp_err_to_name(ret));
  }

  // Give tasks time to finish gracefully
  vTaskDelay(pdMS_TO_TICKS(200));

  // Clean up monitor task
  if (s_agx_monitor.monitor_task_handle) {
    ESP_LOGD(TAG, "Deleting monitor task");
    vTaskDelete(s_agx_monitor.monitor_task_handle);
    s_agx_monitor.monitor_task_handle = NULL;
  }

  // Clean up reconnect task if it exists
  if (s_agx_monitor.reconnect_task_handle) {
    ESP_LOGD(TAG, "Deleting reconnect task");
    vTaskDelete(s_agx_monitor.reconnect_task_handle);
    s_agx_monitor.reconnect_task_handle = NULL;
  }

  // Update connection status
  agx_monitor_update_status(AGX_MONITOR_STATUS_INITIALIZED);

  // Invalidate data and update statistics
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(1000))) {
    s_agx_monitor.latest_data.is_valid = false;

    // Update connected time statistics
    uint64_t current_time = esp_timer_get_time();
    if (s_agx_monitor.connection_status == AGX_MONITOR_STATUS_CONNECTED) {
      s_agx_monitor.connected_time_us +=
          (current_time - s_agx_monitor.start_time_us);
    }

    xSemaphoreGive(s_agx_monitor.data_mutex);
  } else {
    ESP_LOGW(TAG, "Failed to acquire mutex during stop");
  }

  // Trigger disconnected event
  agx_monitor_trigger_event(AGX_MONITOR_EVENT_DISCONNECTED, NULL);

  ESP_LOGD(TAG, "AGX monitor stopped successfully");
  ESP_LOGI(
      TAG,
      "Runtime statistics - Messages: %lu, Reconnects: %lu, Parse errors: %lu",
      s_agx_monitor.messages_received, s_agx_monitor.total_reconnects,
      s_agx_monitor.parse_errors);

  return ESP_OK;
}

bool agx_monitor_is_initialized(void) { return s_agx_monitor.initialized; }

bool agx_monitor_is_running(void) { return s_agx_monitor.running; }

esp_err_t agx_monitor_get_status(agx_monitor_status_info_t *status) {
  if (!s_agx_monitor.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex to ensure consistent data
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(1000))) {
    status->initialized = s_agx_monitor.initialized;
    status->connection_status = s_agx_monitor.connection_status;
    status->running = s_agx_monitor.running;
    status->total_reconnects = s_agx_monitor.total_reconnects;
    status->messages_received = s_agx_monitor.messages_received;
    status->parse_errors = s_agx_monitor.parse_errors;
    status->last_message_time_us = s_agx_monitor.last_message_time_us;

    uint64_t current_time = esp_timer_get_time();
    status->uptime_ms = (current_time - s_agx_monitor.start_time_us) / 1000;
    status->connected_time_ms = s_agx_monitor.connected_time_us / 1000;

    // Calculate connection reliability
    if (status->uptime_ms > 0) {
      status->connection_reliability =
          (float)status->connected_time_ms / (float)status->uptime_ms * 100.0f;
    } else {
      status->connection_reliability = 0.0f;
    }

    strncpy(status->last_error, s_agx_monitor.last_error,
            AGX_MONITOR_MAX_ERROR_MSG_LENGTH - 1);

    xSemaphoreGive(s_agx_monitor.data_mutex);
  } else {
    ESP_LOGW(TAG, "Failed to acquire mutex for status");
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

esp_err_t agx_monitor_get_latest_data(agx_monitor_data_t *data) {
  if (!s_agx_monitor.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex and copy data
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(1000))) {
    memcpy(data, &s_agx_monitor.latest_data, sizeof(agx_monitor_data_t));
    xSemaphoreGive(s_agx_monitor.data_mutex);
  } else {
    ESP_LOGW(TAG, "Failed to acquire mutex for data access");
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

bool agx_monitor_is_data_valid(void) {
  if (!s_agx_monitor.initialized) {
    return false;
  }

  bool is_valid = false;
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(100))) {
    is_valid = s_agx_monitor.latest_data.is_valid;

    // Check if data is recent (within last 30 seconds)
    if (is_valid) {
      uint64_t current_time = esp_timer_get_time();
      uint64_t data_age =
          current_time - s_agx_monitor.latest_data.update_time_us;
      if (data_age > 30000000) { // 30 seconds in microseconds
        ESP_LOGD(TAG, "Data expired: age=%llu us", data_age);
        is_valid = false;
      }
    }

    xSemaphoreGive(s_agx_monitor.data_mutex);
  } else {
    ESP_LOGD(TAG, "Failed to acquire mutex for data validity check");
  }

  return is_valid;
}

uint64_t agx_monitor_get_last_update_time(void) {
  if (!s_agx_monitor.initialized) {
    return 0;
  }

  uint64_t update_time = 0;
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(100))) {
    update_time = s_agx_monitor.latest_data.update_time_us;
    xSemaphoreGive(s_agx_monitor.data_mutex);
  }

  return update_time;
}

esp_err_t agx_monitor_register_callback(agx_monitor_event_callback_t callback,
                                        void *user_data) {
  if (!s_agx_monitor.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (callback == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  s_agx_monitor.event_callback = callback;
  s_agx_monitor.callback_user_data = user_data;

  ESP_LOGI(TAG, "Event callback registered");
  return ESP_OK;
}

esp_err_t agx_monitor_unregister_callback(void) {
  if (!s_agx_monitor.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  s_agx_monitor.event_callback = NULL;
  s_agx_monitor.callback_user_data = NULL;

  ESP_LOGI(TAG, "Event callback unregistered");
  return ESP_OK;
}

/* ============================================================================
 * Private Function Stubs (To be implemented in subsequent phases)
 * ============================================================================
 */

static esp_err_t agx_monitor_websocket_init(void) {
  ESP_LOGI(TAG, "Initializing WebSocket client");

  if (s_agx_monitor.ws_client != NULL) {
    ESP_LOGW(TAG, "WebSocket client already initialized");
    return ESP_OK;
  }

  // Build WebSocket URL with Socket.IO protocol
  char ws_url[256];
  const char *protocol = s_agx_monitor.config.enable_ssl ? "wss" : "ws";

  snprintf(ws_url, sizeof(ws_url),
           "%s://%s:%d/socket.io/?EIO=4&transport=websocket", protocol,
           s_agx_monitor.config.server_url, s_agx_monitor.config.server_port);

  ESP_LOGI(TAG, "WebSocket URL: %s", ws_url);

  // Configure WebSocket client
  esp_websocket_client_config_t ws_config = {
      .uri = ws_url,
      .user_context = NULL,
      .task_stack = s_agx_monitor.config.task_stack_size,
      .task_prio = s_agx_monitor.config.task_priority,
      .buffer_size = 4096, // 4KB buffer for Socket.IO messages
      .ping_interval_sec =
          0, // Disable WebSocket ping (Socket.IO handles heartbeat)
      .pingpong_timeout_sec = 0,   // Disable WebSocket ping timeout
      .network_timeout_ms = 30000, // 30 second network timeout
      .user_agent = "ESP32-robOS-AGX-Monitor/1.0",
      .headers = NULL,
      .cert_pem = NULL, // TODO: Add SSL certificate if needed
      .client_cert = NULL,
      .client_key = NULL,
      .skip_cert_common_name_check = false,
      .keep_alive_enable = false, // Disable TCP keep-alive to avoid conflicts
      .keep_alive_idle = 0,
      .keep_alive_interval = 0,
      .keep_alive_count = 0,
      .reconnect_timeout_ms =
          0, // Disable automatic reconnection (we handle it manually)
      .if_name = NULL};

  // Create WebSocket client
  s_agx_monitor.ws_client = esp_websocket_client_init(&ws_config);
  if (s_agx_monitor.ws_client == NULL) {
    ESP_LOGE(TAG, "Failed to create WebSocket client");
    return ESP_ERR_NO_MEM;
  }

  // Register event handler
  esp_err_t ret = esp_websocket_register_events(
      s_agx_monitor.ws_client, WEBSOCKET_EVENT_ANY,
      agx_monitor_websocket_event_handler, &s_agx_monitor);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WebSocket event handler: %s",
             esp_err_to_name(ret));
    esp_websocket_client_destroy(s_agx_monitor.ws_client);
    s_agx_monitor.ws_client = NULL;
    return ret;
  }

  ESP_LOGI(TAG, "WebSocket client initialized successfully");
  ESP_LOGI(TAG, "Buffer size: 4096 bytes, Ping interval: 10s, Timeout: %lu ms",
           s_agx_monitor.config.heartbeat_timeout_ms);

  return ESP_OK;
}

static esp_err_t agx_monitor_websocket_deinit(void) {
  ESP_LOGI(TAG, "Deinitializing WebSocket client");

  if (s_agx_monitor.ws_client == NULL) {
    ESP_LOGD(TAG, "WebSocket client already deinitialized");
    return ESP_OK;
  }

  // Stop client first
  esp_err_t ret = esp_websocket_client_stop(s_agx_monitor.ws_client);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Error stopping WebSocket client: %s", esp_err_to_name(ret));
  }

  // Give some time for clean shutdown
  vTaskDelay(pdMS_TO_TICKS(100));

  // Destroy client
  ret = esp_websocket_client_destroy(s_agx_monitor.ws_client);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Error destroying WebSocket client: %s",
             esp_err_to_name(ret));
  }

  s_agx_monitor.ws_client = NULL;

  ESP_LOGI(TAG, "WebSocket client deinitialized");
  return ESP_OK;
}

static esp_err_t agx_monitor_connect(void) {
  ESP_LOGD(TAG, "Connecting to AGX server");

  if (s_agx_monitor.ws_client == NULL) {
    ESP_LOGE(TAG, "WebSocket client not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Update status to connecting
  agx_monitor_update_status(AGX_MONITOR_STATUS_CONNECTING);

  // Start WebSocket client
  esp_err_t ret = esp_websocket_client_start(s_agx_monitor.ws_client);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
    agx_monitor_update_status(AGX_MONITOR_STATUS_ERROR);
    agx_monitor_set_error("WebSocket start failed");
    return ret;
  }

  ESP_LOGD(TAG, "WebSocket connection initiated");
  ESP_LOGD(TAG, "Connecting to: %s:%d", s_agx_monitor.config.server_url,
           s_agx_monitor.config.server_port);

  return ESP_OK;
}

static esp_err_t agx_monitor_disconnect(void) {
  ESP_LOGD(TAG, "Disconnecting from AGX server");

  if (s_agx_monitor.ws_client == NULL) {
    ESP_LOGD(TAG, "WebSocket client not initialized");
    return ESP_OK;
  }

  // Check if client is running before stopping
  if (esp_websocket_client_is_connected(s_agx_monitor.ws_client)) {
    ESP_LOGD(TAG, "Closing WebSocket connection");

    // Send close frame
    esp_err_t ret =
        esp_websocket_client_close(s_agx_monitor.ws_client, portMAX_DELAY);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Error sending close frame: %s", esp_err_to_name(ret));
    }
  }

  // Stop the client
  esp_err_t ret = esp_websocket_client_stop(s_agx_monitor.ws_client);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Error stopping WebSocket client: %s", esp_err_to_name(ret));
  }

  // Update status
  agx_monitor_update_status(AGX_MONITOR_STATUS_DISCONNECTED);

  ESP_LOGD(TAG, "WebSocket disconnected");
  return ESP_OK;
}

static void agx_monitor_task(void *pvParameters) {
  ESP_LOGD(TAG, "AGX monitor task started (stack: %lu bytes, priority: %d)",
           s_agx_monitor.config.task_stack_size,
           s_agx_monitor.config.task_priority);

  uint32_t loop_count = 0;
  uint32_t connection_check_counter = 0;
  bool connection_attempted = false;
  bool startup_delay_completed = false;
  uint64_t task_start_time = esp_timer_get_time();

  // Apply startup delay if configured
  if (s_agx_monitor.config.startup_delay_ms > 0) {
    ESP_LOGD(TAG, "Waiting %lu ms for AGX system to boot up...",
             s_agx_monitor.config.startup_delay_ms);
  }

  while (s_agx_monitor.running) {
    loop_count++;
    connection_check_counter++;

    // Check if startup delay period has passed
    if (!startup_delay_completed && s_agx_monitor.config.startup_delay_ms > 0) {
      uint64_t elapsed_ms = (esp_timer_get_time() - task_start_time) / 1000;
      if (elapsed_ms < s_agx_monitor.config.startup_delay_ms) {
        // Still in startup delay period, wait and continue
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
        continue;
      } else {
        startup_delay_completed = true;
        ESP_LOGD(TAG, "AGX startup delay completed, ready to connect");
      }
    } else {
      startup_delay_completed = true; // No delay configured
    }

    // Try to establish connection if not connected
    if (!connection_attempted ||
        (s_agx_monitor.connection_status == AGX_MONITOR_STATUS_DISCONNECTED ||
         s_agx_monitor.connection_status == AGX_MONITOR_STATUS_ERROR)) {

      if (!connection_attempted) {
        ESP_LOGD(TAG, "Attempting initial connection to AGX server");
      } else {
        ESP_LOGD(TAG, "Attempting to reconnect to AGX server (attempt #%lu)",
                 s_agx_monitor.total_reconnects + 1);
        s_agx_monitor.total_reconnects++;
        agx_monitor_trigger_event(AGX_MONITOR_EVENT_RECONNECTING, NULL);
      }

      esp_err_t ret = agx_monitor_connect();
      if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Connection attempt failed: %s", esp_err_to_name(ret));
        agx_monitor_set_error("Connection failed");

        // Wait before next attempt - use fast retry initially, then fixed
        // interval
        uint32_t delay_ms;
        if (s_agx_monitor.total_reconnects <
            s_agx_monitor.config.fast_retry_count) {
          delay_ms = s_agx_monitor.config.fast_retry_interval_ms;
          ESP_LOGD(TAG, "Fast retry mode: waiting %lu ms", delay_ms);
        } else {
          delay_ms = s_agx_monitor.config.reconnect_interval_ms;
          ESP_LOGD(TAG, "Fixed interval mode: waiting %lu ms", delay_ms);
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
      } else {
        connection_attempted = true;
        ESP_LOGD(TAG, "Connection attempt initiated successfully");
      }
    }

    // Update statistics periodically
    if (connection_check_counter % 12 == 0) { // Every 60 seconds (12 * 5s)
      agx_monitor_update_statistics();
    }

    // Connection health check every 6 loops (30 seconds)
    if (loop_count % 6 == 0) {
      const char *status_names[] = {
          "UNINITIALIZED", "INITIALIZED",  "CONNECTING", "CONNECTED",
          "DISCONNECTED",  "RECONNECTING", "ERROR"};
      const char *status_name =
          (s_agx_monitor.connection_status < 7)
              ? status_names[s_agx_monitor.connection_status]
              : "UNKNOWN";

      ESP_LOGD(
          TAG,
          "Health check - Loop: %lu, Status: %s, Messages: %lu, Errors: %lu",
          loop_count, status_name, s_agx_monitor.messages_received,
          s_agx_monitor.parse_errors);

      // Check if we have recent data
      if (s_agx_monitor.connection_status == AGX_MONITOR_STATUS_CONNECTED) {
        uint64_t current_time = esp_timer_get_time();
        uint64_t last_data_age =
            current_time - s_agx_monitor.last_message_time_us;

        if (last_data_age >
            45000000) { // 45 seconds - trigger reconnect (reduced from 90s)
          ESP_LOGW(
              TAG,
              "⚠️  No data received for %llu seconds - triggering reconnect",
              last_data_age / 1000000);

          // Disconnect immediately
          if (s_agx_monitor.ws_client != NULL) {
            ESP_LOGI(TAG, "🔌 Stopping WebSocket client due to data timeout");
            esp_websocket_client_stop(s_agx_monitor.ws_client);
            agx_monitor_update_status(AGX_MONITOR_STATUS_DISCONNECTED);
            agx_monitor_set_error("Data reception timeout");
          }

          // Wait a bit before reconnecting
          vTaskDelay(pdMS_TO_TICKS(3000));

          // Attempt reconnect
          ESP_LOGD(TAG, "Attempting reconnect after data timeout");
          esp_err_t reconnect_ret = agx_monitor_connect();
          if (reconnect_ret != ESP_OK) {
            ESP_LOGW(TAG, "Reconnect failed after timeout: %s",
                     esp_err_to_name(reconnect_ret));
          }
        } else if (last_data_age > 30000000) { // 30 seconds - warning only
          ESP_LOGW(TAG, "No data received for %llu seconds",
                   last_data_age / 1000000);
        } else {
          ESP_LOGD(TAG, "Last data received %llu seconds ago",
                   last_data_age / 1000000);
        }
      }
    }

    // Main loop delay
    vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second intervals
  }

  ESP_LOGI(TAG, "AGX monitor task finishing after %lu loops", loop_count);

  // Disconnect before exiting
  if (s_agx_monitor.connection_status == AGX_MONITOR_STATUS_CONNECTED) {
    agx_monitor_disconnect();
  }

  ESP_LOGI(TAG,
           "AGX monitor task finished - Final stats: Messages: %lu, "
           "Reconnects: %lu, Errors: %lu",
           s_agx_monitor.messages_received, s_agx_monitor.total_reconnects,
           s_agx_monitor.parse_errors);

  // Clean up task handle before exiting
  s_agx_monitor.monitor_task_handle = NULL;
  vTaskDelete(NULL);
}

static void agx_monitor_reconnect_task(void *pvParameters) {
  ESP_LOGD(TAG, "AGX reconnect task started");

  // TODO: Implement reconnection logic in phase 6
  ESP_LOGD(TAG, "Reconnect task - TODO: implement in phase 6");

  vTaskDelete(NULL);
}

static void agx_monitor_websocket_event_handler(void *handler_args,
                                                esp_event_base_t base,
                                                int32_t event_id,
                                                void *event_data) {
  agx_monitor_state_t *state = (agx_monitor_state_t *)handler_args;
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGD(TAG, "Connected to AGX server successfully");
    ESP_LOGD(TAG, "    Server: %s:%d", state->config.server_url,
             state->config.server_port);
    ESP_LOGD(TAG, "    Connection attempts: %lu", state->total_reconnects);

    agx_monitor_update_status(AGX_MONITOR_STATUS_CONNECTED);
    state->total_reconnects =
        0; // Reset reconnect counter on successful connection
    agx_monitor_trigger_event(AGX_MONITOR_EVENT_CONNECTED, NULL);

    // Send Socket.IO connection message
    const char *socketio_connect =
        "40"; // Socket.IO connect message for namespace "/"
    esp_err_t ret =
        esp_websocket_client_send_text(state->ws_client, socketio_connect,
                                       strlen(socketio_connect), portMAX_DELAY);
    if (ret != ESP_OK) {
      ESP_LOGD(TAG, "Failed to send Socket.IO connect message: %s",
               esp_err_to_name(ret));
    } else {
      ESP_LOGD(TAG, "Sent Socket.IO connect message: %s", socketio_connect);
      ESP_LOGD(TAG, "Waiting for Socket.IO connection acknowledgment...");
    }
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "🔌 WebSocket disconnected from AGX server");
    ESP_LOGW(TAG, "    Total messages received: %lu", state->messages_received);
    ESP_LOGW(TAG, "    Parse errors: %lu", state->parse_errors);

    agx_monitor_update_status(AGX_MONITOR_STATUS_DISCONNECTED);

    // Invalidate data on disconnection
    if (xSemaphoreTake(state->data_mutex, pdMS_TO_TICKS(1000))) {
      state->latest_data.is_valid = false;
      xSemaphoreGive(state->data_mutex);
    }

    agx_monitor_trigger_event(AGX_MONITOR_EVENT_DISCONNECTED, NULL);

    // Schedule automatic reconnection
    ESP_LOGD(TAG, "Scheduling automatic reconnection in 3 seconds");
    // Note: The main monitor task will handle reconnection attempts
    break;

  case WEBSOCKET_EVENT_DATA:
    if (data->data_len > 0 && data->data_ptr != NULL) {
      ESP_LOGD(TAG, "Received WebSocket data: %d bytes", data->data_len);

      // Create null-terminated string for processing
      char *message = malloc(data->data_len + 1);
      if (message) {
        memcpy(message, data->data_ptr, data->data_len);
        message[data->data_len] = '\0';

        ESP_LOGD(TAG, "=== WebSocket Raw Message ===");
        ESP_LOGD(TAG, "Length: %d bytes", data->data_len);
        ESP_LOGD(TAG, "Content: %s", message);
        ESP_LOGD(TAG, "============================="); // Handle Socket.IO
                                                        // protocol messages
        if (message[0] == '0') {
          // Socket.IO connection response (type 0)
          ESP_LOGD(TAG, "Socket.IO connection response received");
          ESP_LOGD(TAG, "Connection response: %s", message);
        } else if (message[0] == '4' && message[1] == '0') {
          // Socket.IO connection established (type 40)
          ESP_LOGD(TAG, "Socket.IO connection established");
          ESP_LOGD(TAG, "Connection acknowledgment: %s", message);
        } else if (message[0] == '4' && message[1] == '2') {
          // Socket.IO event message (42 prefix)
          ESP_LOGD(TAG, "📨 Detected Socket.IO event message (42 prefix)");

          // Parse the JSON data after the "42" prefix
          const char *json_start = strchr(message + 2, '[');
          if (json_start) {
            ESP_LOGD(TAG, "Found JSON array start: %s", json_start);

            // Find the event name and data
            // Expected format: 42["tegrastats_update",{data}]
            if (strstr(json_start, "tegrastats_update")) {
              ESP_LOGD(TAG, "*** TEGRASTATS_UPDATE EVENT DETECTED ***");

              const char *data_start = strchr(json_start, '{');
              if (data_start) {
                // Find the end of the JSON object
                const char *data_end = strrchr(message, '}');
                if (data_end) {
                  size_t json_len = data_end - data_start + 1;

                  ESP_LOGD(TAG, "=== TEGRASTATS JSON DATA ===");
                  ESP_LOGD(TAG, "JSON Length: %zu bytes", json_len);
                  ESP_LOGD(TAG, "JSON Content: %.*s", (int)json_len,
                           data_start);
                  ESP_LOGD(TAG, "============================");

                  esp_err_t parse_ret =
                      agx_monitor_parse_data(data_start, json_len);
                  if (parse_ret == ESP_OK) {
                    state->messages_received++;
                    state->last_message_time_us = esp_timer_get_time();
                    ESP_LOGD(TAG, "✅ Processed tegrastats data (msg #%lu)",
                             state->messages_received);
                  } else {
                    state->parse_errors++;
                    ESP_LOGW(TAG, "❌ Failed to parse tegrastats data: %s",
                             esp_err_to_name(parse_ret));
                  }
                } else {
                  ESP_LOGW(TAG, "Could not find JSON object end");
                }
              } else {
                ESP_LOGW(TAG, "Could not find JSON object start");
              }
            } else {
              ESP_LOGD(TAG, "Socket.IO event (not tegrastats_update): %s",
                       json_start);
            }
          } else {
            ESP_LOGW(TAG, "Could not find JSON array in Socket.IO message");
          }
        } else if (message[0] == '4' && message[1] == '0') {
          // Socket.IO connect acknowledgment
          ESP_LOGD(TAG, "Socket.IO connection established");
        } else if (message[0] == '3') {
          // Socket.IO heartbeat/ping - respond with pong
          ESP_LOGD(TAG, "💓 Received Socket.IO ping, sending pong");
          esp_err_t pong_ret = esp_websocket_client_send_text(
              state->ws_client, "3", 1, portMAX_DELAY);
          if (pong_ret == ESP_OK) {
            ESP_LOGD(TAG, "💓 Pong sent successfully");
          } else {
            ESP_LOGW(TAG, "💓 Failed to send pong: %s",
                     esp_err_to_name(pong_ret));
          }
        } else if (message[0] == '2') {
          // Socket.IO ping
          ESP_LOGD(TAG, "Socket.IO ping (type 2)");
        } else {
          // Handle unknown or binary data more gracefully
          if (data->data_len <= 0 || data->data_len > 1024) {
            ESP_LOGW(TAG, "Received invalid message length: %d bytes",
                     data->data_len);
          } else if (data->data_len <= 2) {
            // Short messages (1-2 bytes) could be abnormal data
            bool is_abnormal = false;

            // Check if this is abnormal binary data (non-printable characters)
            for (int i = 0; i < data->data_len; i++) {
              unsigned char byte = (unsigned char)message[i];
              if (byte < 32 && byte != '\n' && byte != '\r' && byte != '\t') {
                is_abnormal = true;
                break;
              }
            }

            if (is_abnormal) {
              ESP_LOGD(TAG, "ABNORMAL DATA DETECTED: %d bytes", data->data_len);
              for (int i = 0; i < data->data_len; i++) {
                unsigned char byte_val = (unsigned char)message[i];
                ESP_LOGD(TAG, "   Byte %d: 0x%02X ('%c')", i, byte_val,
                         isprint(byte_val) ? byte_val : '?');
              }
              ESP_LOGD(TAG, "Connection appears unstable - forcing reconnect");

              // Force immediate disconnect and reconnect
              esp_websocket_client_stop(state->ws_client);
              agx_monitor_update_status(AGX_MONITOR_STATUS_DISCONNECTED);
              agx_monitor_set_error("Abnormal data received");

              // The monitor task will handle reconnection
              free(message);
              return;
            } else {
              ESP_LOGD(TAG,
                       "🏓 Short message (%d bytes) - likely control frame",
                       data->data_len);
            }
          } else if (data->data_len < 10 && (unsigned char)message[0] < 32) {
            // Likely binary data or control frames - but could be abnormal
            ESP_LOGW(TAG,
                     "⚠️  SUSPICIOUS BINARY DATA: %d bytes, first byte: 0x%02X",
                     data->data_len, (unsigned char)message[0]);
            ESP_LOGW(TAG, "� Potential connection issue - forcing reconnect");

            // Force immediate disconnect and reconnect for suspicious binary
            // data
            esp_websocket_client_stop(state->ws_client);
            agx_monitor_update_status(AGX_MONITOR_STATUS_DISCONNECTED);
            agx_monitor_set_error("Suspicious binary data received");

            free(message);
            return;
          } else {
            ESP_LOGW(TAG, "❓ Unknown Socket.IO message type: %s", message);
            ESP_LOGW(TAG, "   First char: '%c' (0x%02X)", message[0],
                     (unsigned char)message[0]);
            if (strlen(message) > 1) {
              ESP_LOGW(TAG, "   Second char: '%c' (0x%02X)", message[1],
                       (unsigned char)message[1]);
            }
          }
        }

        free(message);
      } else {
        ESP_LOGE(TAG, "Failed to allocate memory for WebSocket message");
      }
    }
    break;

  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG, "WebSocket error occurred");
    agx_monitor_update_status(AGX_MONITOR_STATUS_ERROR);
    agx_monitor_set_error("WebSocket error");
    agx_monitor_trigger_event(AGX_MONITOR_EVENT_ERROR, data);
    break;

  case WEBSOCKET_EVENT_BEFORE_CONNECT:
    ESP_LOGD(TAG, "WebSocket preparing to connect");
    break;

  default:
    ESP_LOGD(TAG, "Unknown WebSocket event: %ld", event_id);
    break;
  }
}

static esp_err_t agx_monitor_parse_data(const char *json_data,
                                        size_t data_len) {
  if (json_data == NULL || data_len == 0) {
    ESP_LOGE(TAG, "Invalid JSON data parameters");
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGD(TAG, "Parsing JSON data (%zu bytes): %.100s%s", data_len, json_data,
           data_len > 100 ? "..." : "");

  // Parse JSON data using cJSON
  cJSON *root = cJSON_Parse(json_data);
  if (root == NULL) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
    } else {
      ESP_LOGE(TAG, "Failed to parse JSON data");
    }
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = ESP_OK;

  // Acquire mutex for data update
  if (xSemaphoreTake(s_agx_monitor.data_mutex, pdMS_TO_TICKS(1000))) {
    // Clear previous data
    memset(&s_agx_monitor.latest_data, 0, sizeof(agx_monitor_data_t));
    s_agx_monitor.latest_data.update_time_us = esp_timer_get_time();

    // Parse timestamp
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsString(timestamp) && (timestamp->valuestring != NULL)) {
      strncpy(s_agx_monitor.latest_data.timestamp, timestamp->valuestring,
              sizeof(s_agx_monitor.latest_data.timestamp) - 1);
      ESP_LOGD(TAG, "Parsed timestamp: %s",
               s_agx_monitor.latest_data.timestamp);
    }

    // Parse CPU data
    cJSON *cpu = cJSON_GetObjectItem(root, "cpu");
    if (cpu != NULL) {
      ret = agx_monitor_parse_cpu_data(cpu, &s_agx_monitor.latest_data);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse CPU data: %s", esp_err_to_name(ret));
      }
    }

    // Parse memory data
    cJSON *memory = cJSON_GetObjectItem(root, "memory");
    if (memory != NULL) {
      esp_err_t mem_ret =
          agx_monitor_parse_memory_data(memory, &s_agx_monitor.latest_data);
      if (mem_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse memory data: %s",
                 esp_err_to_name(mem_ret));
        if (ret == ESP_OK)
          ret = mem_ret;
      }
    }

    // Parse temperature data
    cJSON *temperature = cJSON_GetObjectItem(root, "temperature");
    if (temperature != NULL) {
      esp_err_t temp_ret = agx_monitor_parse_temperature_data(
          temperature, &s_agx_monitor.latest_data);
      if (temp_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse temperature data: %s",
                 esp_err_to_name(temp_ret));
        if (ret == ESP_OK)
          ret = temp_ret;
      }
    }

    // Parse power data
    cJSON *power = cJSON_GetObjectItem(root, "power");
    if (power != NULL) {
      esp_err_t power_ret =
          agx_monitor_parse_power_data(power, &s_agx_monitor.latest_data);
      if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse power data: %s",
                 esp_err_to_name(power_ret));
        if (ret == ESP_OK)
          ret = power_ret;
      }
    }

    // Parse GPU data
    cJSON *gpu = cJSON_GetObjectItem(root, "gpu");
    if (gpu != NULL) {
      esp_err_t gpu_ret =
          agx_monitor_parse_gpu_data(gpu, &s_agx_monitor.latest_data);
      if (gpu_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse GPU data: %s", esp_err_to_name(gpu_ret));
        if (ret == ESP_OK)
          ret = gpu_ret;
      }
    }

    // Mark data as valid if parsing was successful
    if (ret == ESP_OK) {
      s_agx_monitor.latest_data.is_valid = true;
      ESP_LOGD(TAG, "JSON data parsing completed successfully");
    } else {
      s_agx_monitor.latest_data.is_valid = false;
      ESP_LOGW(TAG, "JSON data parsing completed with errors");
    }

    xSemaphoreGive(s_agx_monitor.data_mutex);
  } else {
    ESP_LOGE(TAG, "Failed to acquire mutex for data update");
    ret = ESP_ERR_TIMEOUT;
  }

  // Clean up JSON object
  cJSON_Delete(root);

  // Trigger data received event
  agx_monitor_trigger_event(AGX_MONITOR_EVENT_DATA_RECEIVED, NULL);

  return ret;
}

static esp_err_t agx_monitor_parse_cpu_data(cJSON *cpu_json,
                                            agx_monitor_data_t *data) {
  if (cpu_json == NULL || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *cores = cJSON_GetObjectItem(cpu_json, "cores");
  if (!cJSON_IsArray(cores)) {
    ESP_LOGW(TAG, "CPU cores data is not an array");
    return ESP_ERR_INVALID_ARG;
  }

  int core_count = cJSON_GetArraySize(cores);
  if (core_count > AGX_MONITOR_MAX_CPU_CORES) {
    ESP_LOGW(TAG, "Too many CPU cores: %d, limiting to %d", core_count,
             AGX_MONITOR_MAX_CPU_CORES);
    core_count = AGX_MONITOR_MAX_CPU_CORES;
  }

  data->cpu.core_count = core_count;

  for (int i = 0; i < core_count; i++) {
    cJSON *core = cJSON_GetArrayItem(cores, i);
    if (core == NULL)
      continue;

    cJSON *id = cJSON_GetObjectItem(core, "id");
    cJSON *usage = cJSON_GetObjectItem(core, "usage");
    cJSON *freq = cJSON_GetObjectItem(core, "freq");

    if (cJSON_IsNumber(id)) {
      data->cpu.cores[i].id = (uint8_t)id->valueint;
    }
    if (cJSON_IsNumber(usage)) {
      data->cpu.cores[i].usage = (uint8_t)usage->valueint;
    }
    if (cJSON_IsNumber(freq)) {
      data->cpu.cores[i].freq = (uint16_t)freq->valueint;
    }

    ESP_LOGD(TAG, "CPU Core %d: usage=%d%%, freq=%dMHz", data->cpu.cores[i].id,
             data->cpu.cores[i].usage, data->cpu.cores[i].freq);
  }

  ESP_LOGD(TAG, "Parsed CPU data: %d cores", data->cpu.core_count);
  return ESP_OK;
}

static esp_err_t agx_monitor_parse_memory_data(cJSON *memory_json,
                                               agx_monitor_data_t *data) {
  if (memory_json == NULL || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Parse RAM data
  cJSON *ram = cJSON_GetObjectItem(memory_json, "ram");
  if (cJSON_IsObject(ram)) {
    cJSON *used = cJSON_GetObjectItem(ram, "used");
    cJSON *total = cJSON_GetObjectItem(ram, "total");
    cJSON *unit = cJSON_GetObjectItem(ram, "unit");

    if (cJSON_IsNumber(used)) {
      data->memory.ram.used = (uint32_t)used->valueint;
    }
    if (cJSON_IsNumber(total)) {
      data->memory.ram.total = (uint32_t)total->valueint;
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->memory.ram.unit, unit->valuestring,
              sizeof(data->memory.ram.unit) - 1);
    }

    ESP_LOGD(TAG, "RAM: %lu/%lu %s", data->memory.ram.used,
             data->memory.ram.total, data->memory.ram.unit);
  }

  // Parse Swap data
  cJSON *swap = cJSON_GetObjectItem(memory_json, "swap");
  if (cJSON_IsObject(swap)) {
    cJSON *used = cJSON_GetObjectItem(swap, "used");
    cJSON *total = cJSON_GetObjectItem(swap, "total");
    cJSON *cached = cJSON_GetObjectItem(swap, "cached");
    cJSON *unit = cJSON_GetObjectItem(swap, "unit");

    if (cJSON_IsNumber(used)) {
      data->memory.swap.used = (uint32_t)used->valueint;
    }
    if (cJSON_IsNumber(total)) {
      data->memory.swap.total = (uint32_t)total->valueint;
    }
    if (cJSON_IsNumber(cached)) {
      data->memory.swap.cached = (uint32_t)cached->valueint;
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->memory.swap.unit, unit->valuestring,
              sizeof(data->memory.swap.unit) - 1);
    }

    ESP_LOGD(TAG, "Swap: %lu/%lu (cached: %lu) %s", data->memory.swap.used,
             data->memory.swap.total, data->memory.swap.cached,
             data->memory.swap.unit);
  }

  ESP_LOGD(TAG, "Parsed memory data successfully");
  return ESP_OK;
}

static esp_err_t agx_monitor_parse_temperature_data(cJSON *temp_json,
                                                    agx_monitor_data_t *data) {
  if (temp_json == NULL || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *cpu = cJSON_GetObjectItem(temp_json, "cpu");
  if (cJSON_IsNumber(cpu)) {
    data->temperature.cpu = (float)cpu->valuedouble;
    ESP_LOGD(TAG, "CPU temperature: %.1f°C", data->temperature.cpu);

    // Push CPU temperature to console temperature system for fan control
    esp_err_t temp_ret = console_set_agx_temperature(data->temperature.cpu);
    if (temp_ret != ESP_OK) {
      ESP_LOGD(TAG, "Failed to update AGX temperature: %s",
               esp_err_to_name(temp_ret));
    }
  }

  cJSON *soc0 = cJSON_GetObjectItem(temp_json, "soc0");
  if (cJSON_IsNumber(soc0)) {
    data->temperature.soc0 = (float)soc0->valuedouble;
    ESP_LOGD(TAG, "SoC0 temperature: %.1f°C", data->temperature.soc0);
  }

  cJSON *soc1 = cJSON_GetObjectItem(temp_json, "soc1");
  if (cJSON_IsNumber(soc1)) {
    data->temperature.soc1 = (float)soc1->valuedouble;
    ESP_LOGD(TAG, "SoC1 temperature: %.1f°C", data->temperature.soc1);
  }

  cJSON *soc2 = cJSON_GetObjectItem(temp_json, "soc2");
  if (cJSON_IsNumber(soc2)) {
    data->temperature.soc2 = (float)soc2->valuedouble;
    ESP_LOGD(TAG, "SoC2 temperature: %.1f°C", data->temperature.soc2);
  }

  cJSON *tj = cJSON_GetObjectItem(temp_json, "tj");
  if (cJSON_IsNumber(tj)) {
    data->temperature.tj = (float)tj->valuedouble;
    ESP_LOGD(TAG, "TJ temperature: %.1f°C", data->temperature.tj);
  }

  ESP_LOGD(TAG, "Parsed temperature data successfully");
  return ESP_OK;
}

static esp_err_t agx_monitor_parse_power_data(cJSON *power_json,
                                              agx_monitor_data_t *data) {
  if (power_json == NULL || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Note: AGX server sends power consumption data for RAM and swap in power
  // section This is separate from memory usage data in memory section Power
  // section: RAM/swap power consumption in mW Memory section: RAM/swap memory
  // usage in MB

  // Try to parse GPU+SoC power (old format)
  cJSON *gpu_soc = cJSON_GetObjectItem(power_json, "gpu_soc");
  if (cJSON_IsObject(gpu_soc)) {
    cJSON *current = cJSON_GetObjectItem(gpu_soc, "current");
    cJSON *average = cJSON_GetObjectItem(gpu_soc, "average");
    cJSON *unit = cJSON_GetObjectItem(gpu_soc, "unit");

    if (cJSON_IsNumber(current)) {
      data->power.gpu_soc.current = (uint32_t)current->valueint;
    }
    if (cJSON_IsNumber(average)) {
      data->power.gpu_soc.average = (uint32_t)average->valueint;
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->power.gpu_soc.unit, unit->valuestring,
              sizeof(data->power.gpu_soc.unit) - 1);
    }
    ESP_LOGD(TAG, "GPU+SoC power: %lu/%lu %s", data->power.gpu_soc.current,
             data->power.gpu_soc.average, data->power.gpu_soc.unit);
  }

  // Try to parse CPU power (old format)
  cJSON *cpu_cv = cJSON_GetObjectItem(power_json, "cpu_cv");
  if (cJSON_IsObject(cpu_cv)) {
    cJSON *current = cJSON_GetObjectItem(cpu_cv, "current");
    cJSON *average = cJSON_GetObjectItem(cpu_cv, "average");
    cJSON *unit = cJSON_GetObjectItem(cpu_cv, "unit");

    if (cJSON_IsNumber(current)) {
      data->power.cpu_cv.current = (uint32_t)current->valueint;
    }
    if (cJSON_IsNumber(average)) {
      data->power.cpu_cv.average = (uint32_t)average->valueint;
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->power.cpu_cv.unit, unit->valuestring,
              sizeof(data->power.cpu_cv.unit) - 1);
    }
    ESP_LOGD(TAG, "CPU power: %lu/%lu %s", data->power.cpu_cv.current,
             data->power.cpu_cv.average, data->power.cpu_cv.unit);
  }

  // Try to parse system 5V power (old format)
  cJSON *sys_5v = cJSON_GetObjectItem(power_json, "sys_5v");
  if (cJSON_IsObject(sys_5v)) {
    cJSON *current = cJSON_GetObjectItem(sys_5v, "current");
    cJSON *average = cJSON_GetObjectItem(sys_5v, "average");
    cJSON *unit = cJSON_GetObjectItem(sys_5v, "unit");

    if (cJSON_IsNumber(current)) {
      data->power.sys_5v.current = (uint32_t)current->valueint;
    }
    if (cJSON_IsNumber(average)) {
      data->power.sys_5v.average = (uint32_t)average->valueint;
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->power.sys_5v.unit, unit->valuestring,
              sizeof(data->power.sys_5v.unit) - 1);
    }
    ESP_LOGD(TAG, "System 5V power: %lu/%lu %s", data->power.sys_5v.current,
             data->power.sys_5v.average, data->power.sys_5v.unit);
  }

  // Parse RAM power consumption data
  cJSON *ram_power = cJSON_GetObjectItem(power_json, "ram");
  if (cJSON_IsObject(ram_power)) {
    cJSON *current = cJSON_GetObjectItem(ram_power, "current");
    cJSON *average = cJSON_GetObjectItem(ram_power, "average");
    cJSON *unit = cJSON_GetObjectItem(ram_power, "unit");

    if (cJSON_IsNumber(current)) {
      data->power.ram.current = (uint32_t)current->valueint;
    }
    if (cJSON_IsNumber(average)) {
      // Check if average contains memory size (AGX server bug) or actual power
      // average
      if (average->valueint > 50000) {
        ESP_LOGD(TAG,
                 "AGX server bug detected: RAM power average field contains "
                 "memory size (%d MB), ignoring",
                 average->valueint);
        data->power.ram.average =
            data->power.ram.current; // Use current as fallback
      } else {
        data->power.ram.average = (uint32_t)average->valueint;
      }
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->power.ram.unit, unit->valuestring,
              sizeof(data->power.ram.unit) - 1);
    }

    ESP_LOGD(TAG, "RAM power: %lu/%lu %s", data->power.ram.current,
             data->power.ram.average, data->power.ram.unit);
  }

  // Parse Swap power consumption data
  cJSON *swap_power = cJSON_GetObjectItem(power_json, "swap");
  if (cJSON_IsObject(swap_power)) {
    cJSON *current = cJSON_GetObjectItem(swap_power, "current");
    cJSON *average = cJSON_GetObjectItem(swap_power, "average");
    cJSON *unit = cJSON_GetObjectItem(swap_power, "unit");

    if (cJSON_IsNumber(current)) {
      data->power.swap.current = (uint32_t)current->valueint;
    }
    if (cJSON_IsNumber(average)) {
      // Check if average contains memory size (AGX server bug) or actual power
      // average
      if (average->valueint > 30000) {
        ESP_LOGD(TAG,
                 "AGX server bug detected: Swap power average field contains "
                 "memory size (%d MB), ignoring",
                 average->valueint);
        data->power.swap.average =
            data->power.swap.current; // Use current as fallback
      } else {
        data->power.swap.average = (uint32_t)average->valueint;
      }
    }
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
      strncpy(data->power.swap.unit, unit->valuestring,
              sizeof(data->power.swap.unit) - 1);
    }

    ESP_LOGD(TAG, "Swap power: %lu/%lu %s", data->power.swap.current,
             data->power.swap.average, data->power.swap.unit);
  }

  // Log successful parsing of RAM/Swap power data
  if (ram_power != NULL || swap_power != NULL) {
    ESP_LOGD(TAG, "✅ Successfully parsed RAM/Swap power consumption data");
  }

  ESP_LOGD(TAG, "Parsed power data successfully");
  return ESP_OK;
}

static esp_err_t agx_monitor_parse_gpu_data(cJSON *gpu_json,
                                            agx_monitor_data_t *data) {
  if (gpu_json == NULL || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *gr3d_freq = cJSON_GetObjectItem(gpu_json, "gr3d_freq");
  if (cJSON_IsNumber(gr3d_freq)) {
    data->gpu.gr3d_freq = (uint8_t)gr3d_freq->valueint;
    ESP_LOGD(TAG, "GPU GR3D frequency: %d%%", data->gpu.gr3d_freq);
  }

  ESP_LOGD(TAG, "Parsed GPU data successfully");
  return ESP_OK;
}

static void agx_monitor_update_status(agx_monitor_status_t new_status) {
  agx_monitor_status_t old_status = s_agx_monitor.connection_status;
  if (old_status != new_status) {
    s_agx_monitor.connection_status = new_status;

    // Log status change with descriptive names
    const char *status_names[] = {
        "UNINITIALIZED", "INITIALIZED",  "CONNECTING", "CONNECTED",
        "DISCONNECTED",  "RECONNECTING", "ERROR"};

    const char *old_name =
        (old_status < 7) ? status_names[old_status] : "UNKNOWN";
    const char *new_name =
        (new_status < 7) ? status_names[new_status] : "UNKNOWN";

    // All status changes at DEBUG level for silent operation
    ESP_LOGD(TAG, "Status changed: %s -> %s", old_name, new_name);

    // Update connected time statistics
    uint64_t current_time = esp_timer_get_time();
    if (old_status == AGX_MONITOR_STATUS_CONNECTED &&
        new_status != AGX_MONITOR_STATUS_CONNECTED) {
      s_agx_monitor.connected_time_us +=
          (current_time - s_agx_monitor.start_time_us);
    }
  }
}

static void agx_monitor_trigger_event(agx_monitor_event_type_t event_type,
                                      void *event_data) {
  if (s_agx_monitor.event_callback) {
    ESP_LOGD(TAG, "Triggering event: %d", event_type);
    s_agx_monitor.event_callback(event_type, event_data,
                                 s_agx_monitor.callback_user_data);
  } else {
    ESP_LOGD(TAG, "Event %d triggered but no callback registered", event_type);
  }
}

static void agx_monitor_update_statistics(void) {
  // Update basic runtime statistics
  uint64_t current_time = esp_timer_get_time();

  // This function will be called periodically to update internal statistics
  // For now, just log debug info periodically
  static uint32_t stats_counter = 0;
  stats_counter++;

  // Log statistics every 60 calls (5 minutes with 5-second intervals)
  if (stats_counter % 60 == 0) {
    uint64_t uptime_ms = (current_time - s_agx_monitor.start_time_us) / 1000;
    ESP_LOGI(TAG,
             "Statistics - Uptime: %llu ms, Messages: %lu, Reconnects: %lu, "
             "Errors: %lu",
             uptime_ms, s_agx_monitor.messages_received,
             s_agx_monitor.total_reconnects, s_agx_monitor.parse_errors);
  }
}

static void agx_monitor_set_error(const char *error_msg) {
  if (error_msg) {
    strncpy(s_agx_monitor.last_error, error_msg,
            AGX_MONITOR_MAX_ERROR_MSG_LENGTH - 1);
    s_agx_monitor.last_error[AGX_MONITOR_MAX_ERROR_MSG_LENGTH - 1] = '\0';
    ESP_LOGD(TAG, "Error set: %s", error_msg);
  }
}

/* ============================================================================
 * Console Commands Implementation (Phase 7)
 * ============================================================================
 */

esp_err_t agx_monitor_register_console_commands(void) {
  ESP_LOGD(TAG, "Console commands registration - TODO: implement in phase 7");
  // TODO: Implement console command registration
  return ESP_OK;
}

static esp_err_t cmd_agx_status(int argc, char **argv) {
  agx_monitor_status_info_t status;
  esp_err_t ret = agx_monitor_get_status(&status);

  if (ret != ESP_OK) {
    printf("Error getting AGX monitor status: %s\n", esp_err_to_name(ret));
    return ret;
  }

  const char *status_names[] = {"UNINITIALIZED", "INITIALIZED",  "CONNECTING",
                                "CONNECTED",     "DISCONNECTED", "RECONNECTING",
                                "ERROR"};
  const char *status_name = (status.connection_status < 7)
                                ? status_names[status.connection_status]
                                : "UNKNOWN";

  printf("\n=== AGX Monitor Status ===\n");
  printf("Initialized: %s\n", status.initialized ? "Yes" : "No");
  printf("Running: %s\n", status.running ? "Yes" : "No");
  printf("Connection Status: %s\n", status_name);
  printf("Messages Received: %lu\n", status.messages_received);
  printf("Parse Errors: %lu\n", status.parse_errors);
  printf("Total Reconnects: %lu\n", status.total_reconnects);
  printf("Uptime: %.1f seconds\n", status.uptime_ms / 1000.0f);
  printf("Connected Time: %.1f seconds\n", status.connected_time_ms / 1000.0f);
  printf("Connection Reliability: %.1f%%\n", status.connection_reliability);

  if (strlen(status.last_error) > 0) {
    printf("Last Error: %s\n", status.last_error);
  }

  uint64_t current_time = esp_timer_get_time();
  if (status.last_message_time_us > 0) {
    uint64_t last_msg_age =
        (current_time - status.last_message_time_us) / 1000000;
    printf("Last Message: %llu seconds ago\n", last_msg_age);
  } else {
    printf("Last Message: Never\n");
  }

  printf("=========================\n\n");
  return ESP_OK;
}

static esp_err_t cmd_agx_start(int argc, char **argv) {
  printf("Starting AGX Monitor...\n");
  esp_err_t ret = agx_monitor_start();

  if (ret == ESP_OK) {
    printf("AGX Monitor started successfully.\n");
  } else {
    printf("Failed to start AGX Monitor: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t cmd_agx_stop(int argc, char **argv) {
  printf("Stopping AGX Monitor...\n");
  esp_err_t ret = agx_monitor_stop();

  if (ret == ESP_OK) {
    printf("AGX Monitor stopped successfully.\n");
  } else {
    printf("Failed to stop AGX Monitor: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t cmd_agx_data(int argc, char **argv) {
  agx_monitor_data_t data;
  esp_err_t ret = agx_monitor_get_latest_data(&data);

  if (ret != ESP_OK) {
    printf("Error getting AGX data: %s\n", esp_err_to_name(ret));
    return ret;
  }

  if (!data.is_valid) {
    printf("No valid AGX data available yet.\n");
    return ESP_OK;
  }

  printf("\n=== Latest AGX Data ===\n");
  printf("Timestamp: %s\n", data.timestamp);

  // CPU Information
  printf("\n--- CPU Information ---\n");
  printf("Core Count: %d\n", data.cpu.core_count);
  for (int i = 0; i < data.cpu.core_count && i < AGX_MONITOR_MAX_CPU_CORES;
       i++) {
    printf("  Core %d: %d%% @ %d MHz\n", data.cpu.cores[i].id,
           data.cpu.cores[i].usage, data.cpu.cores[i].freq);
  }

  // Memory Information
  printf("\n--- Memory Information ---\n");
  printf("RAM: %lu/%lu %s (%.1f%% used)\n", data.memory.ram.used,
         data.memory.ram.total, data.memory.ram.unit,
         (float)data.memory.ram.used / data.memory.ram.total * 100);
  printf("Swap: %lu/%lu %s (cached: %lu)\n", data.memory.swap.used,
         data.memory.swap.total, data.memory.swap.unit,
         data.memory.swap.cached);

  // Temperature Information
  printf("\n--- Temperature Information ---\n");
  printf("CPU: %.1f°C\n", data.temperature.cpu);
  printf("SoC0: %.1f°C\n", data.temperature.soc0);
  printf("SoC1: %.1f°C\n", data.temperature.soc1);
  printf("SoC2: %.1f°C\n", data.temperature.soc2);
  printf("Junction: %.1f°C\n", data.temperature.tj);

  // Power Information
  printf("\n--- Power Information ---\n");
  printf("RAM Power: %lu/%lu %s\n", data.power.ram.current,
         data.power.ram.average, data.power.ram.unit);
  printf("Swap Power: %lu/%lu %s\n", data.power.swap.current,
         data.power.swap.average, data.power.swap.unit);
  printf("GPU+SoC Power: %lu/%lu %s\n", data.power.gpu_soc.current,
         data.power.gpu_soc.average, data.power.gpu_soc.unit);
  printf("CPU Power: %lu/%lu %s\n", data.power.cpu_cv.current,
         data.power.cpu_cv.average, data.power.cpu_cv.unit);
  printf("System 5V Power: %lu/%lu %s\n", data.power.sys_5v.current,
         data.power.sys_5v.average, data.power.sys_5v.unit);

  // GPU Information
  printf("\n--- GPU Information ---\n");
  printf("GR3D Frequency: %d%%\n", data.gpu.gr3d_freq);

  printf("=====================\n\n");
  return ESP_OK;
}

static esp_err_t cmd_agx_config(int argc, char **argv) {
  if (!s_agx_monitor.initialized) {
    printf("AGX Monitor not initialized.\n");
    return ESP_ERR_INVALID_STATE;
  }

  printf("\n=== AGX Monitor Configuration ===\n");
  printf("Server URL: %s\n", s_agx_monitor.config.server_url);
  printf("Server Port: %d\n", s_agx_monitor.config.server_port);
  printf("Reconnect Interval: %lu ms\n",
         s_agx_monitor.config.reconnect_interval_ms);
  printf("Fast Retry Count: %lu\n", s_agx_monitor.config.fast_retry_count);
  printf("Fast Retry Interval: %lu ms\n",
         s_agx_monitor.config.fast_retry_interval_ms);
  printf("Heartbeat Timeout: %lu ms\n",
         s_agx_monitor.config.heartbeat_timeout_ms);
  printf("SSL Enabled: %s\n", s_agx_monitor.config.enable_ssl ? "Yes" : "No");
  printf("Auto Start: %s\n", s_agx_monitor.config.auto_start ? "Yes" : "No");
  printf("Startup Delay: %lu ms\n", s_agx_monitor.config.startup_delay_ms);
  printf("Task Stack Size: %lu bytes\n", s_agx_monitor.config.task_stack_size);
  printf("Task Priority: %d\n", s_agx_monitor.config.task_priority);
  printf("================================\n\n");

  return ESP_OK;
}

static esp_err_t cmd_agx_stats(int argc, char **argv) {
  agx_monitor_status_info_t status;
  esp_err_t ret = agx_monitor_get_status(&status);

  if (ret != ESP_OK) {
    printf("Error getting AGX monitor statistics: %s\n", esp_err_to_name(ret));
    return ret;
  }

  printf("\n=== AGX Monitor Statistics ===\n");
  printf("Total Messages Received: %lu\n", status.messages_received);
  printf("Parse Errors: %lu\n", status.parse_errors);
  printf("Parse Success Rate: %.2f%%\n",
         status.messages_received > 0
             ? (float)(status.messages_received - status.parse_errors) /
                   status.messages_received * 100
             : 0);
  printf("Total Reconnection Attempts: %lu\n", status.total_reconnects);
  printf("System Uptime: %.1f seconds\n", status.uptime_ms / 1000.0f);
  printf("Connected Time: %.1f seconds\n", status.connected_time_ms / 1000.0f);
  printf("Connection Reliability: %.1f%%\n", status.connection_reliability);

  if (status.messages_received > 0 && status.uptime_ms > 0) {
    float msg_rate =
        (float)status.messages_received / (status.uptime_ms / 1000.0f);
    printf("Average Message Rate: %.2f msg/sec\n", msg_rate);
  }

  if (status.total_reconnects > 0 && status.uptime_ms > 0) {
    float reconnect_rate =
        (float)status.total_reconnects / (status.uptime_ms / 1000.0f / 60.0f);
    printf("Reconnection Rate: %.2f reconnects/min\n", reconnect_rate);
  }

  printf("=============================\n\n");
  return ESP_OK;
}

// Main AGX monitor command handler with subcommands
static esp_err_t cmd_agx_monitor(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: agx_monitor <subcommand> [args]\n");
    printf("Available subcommands:\n");
    printf(
        "  status     - Show AGX monitor connection status and statistics\n");
    printf("  start      - Start AGX monitor\n");
    printf("  stop       - Stop AGX monitor\n");
    printf("  data       - Display latest AGX system data\n");
    printf("  config     - Display AGX monitor configuration\n");
    printf("  stats      - Display detailed AGX monitor statistics\n");
    printf("  debug      - Debug commands (verbose|quiet|normal|reconnect)\n");
    return ESP_ERR_INVALID_ARG;
  }

  const char *subcmd = argv[1];

  if (strcmp(subcmd, "status") == 0) {
    return cmd_agx_status(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "start") == 0) {
    return cmd_agx_start(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "stop") == 0) {
    return cmd_agx_stop(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "data") == 0) {
    return cmd_agx_data(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "config") == 0) {
    return cmd_agx_config(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "stats") == 0) {
    return cmd_agx_stats(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "debug") == 0) {
    return cmd_agx_debug(argc - 1, argv + 1);
  } else {
    printf("Unknown subcommand: %s\n", subcmd);
    printf(
        "Use 'agx_monitor' without arguments to see available subcommands\n");
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_agx_debug(int argc, char **argv) {
  if (argc > 1) {
    if (strcmp(argv[1], "verbose") == 0) {
      printf("Enabling verbose debug logging for AGX Monitor...\n");
      esp_log_level_set(TAG, ESP_LOG_DEBUG);
      return ESP_OK;
    } else if (strcmp(argv[1], "quiet") == 0) {
      printf("Setting quiet mode for AGX Monitor...\n");
      esp_log_level_set(TAG, ESP_LOG_WARN);
      return ESP_OK;
    } else if (strcmp(argv[1], "normal") == 0) {
      printf("Setting normal logging for AGX Monitor...\n");
      esp_log_level_set(TAG, ESP_LOG_INFO);
      return ESP_OK;
    } else if (strcmp(argv[1], "reconnect") == 0) {
      printf("Forcing reconnection...\n");
      if (s_agx_monitor.ws_client != NULL) {
        esp_websocket_client_stop(s_agx_monitor.ws_client);
        agx_monitor_update_status(AGX_MONITOR_STATUS_DISCONNECTED);
        printf("Reconnection triggered.\n");
      } else {
        printf("No active connection to reconnect.\n");
      }
      return ESP_OK;
    }
  }

  printf("\n=== AGX Monitor Debug Commands ===\n");
  printf("agx_debug verbose   - Enable verbose debug logging\n");
  printf("agx_debug quiet     - Enable quiet mode (warnings only)\n");
  printf("agx_debug normal    - Normal logging mode\n");
  printf("agx_debug reconnect - Force reconnection\n");
  printf("=================================\n\n");

  return ESP_OK;
}

// Console command registration
static esp_err_t agx_monitor_register_commands(void) {
  // Define AGX monitor main command with subcommands
  static const console_cmd_t agx_commands[] = {
      {.command = "agx_monitor",
       .help = "AGX monitor control and status commands",
       .hint = "<status|start|stop|data|config|stats|debug> [args]",
       .func = cmd_agx_monitor,
       .min_args = 0,
       .max_args = 2}};

  // Register all commands
  size_t cmd_count = sizeof(agx_commands) / sizeof(agx_commands[0]);
  for (size_t i = 0; i < cmd_count; i++) {
    esp_err_t ret = console_register_command(&agx_commands[i]);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register command '%s': %s",
               agx_commands[i].command, esp_err_to_name(ret));
      return ret;
    }
  }

  ESP_LOGD(TAG, "Registered AGX monitor console command with %zu subcommands",
           7);
  return ESP_OK;
}

static esp_err_t agx_monitor_unregister_commands(void) {
  esp_err_t ret = console_unregister_command("agx_monitor");
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to unregister command 'agx_monitor': %s",
             esp_err_to_name(ret));
  }

  ESP_LOGD(TAG, "Unregistered AGX monitor console command");
  return ESP_OK;
}