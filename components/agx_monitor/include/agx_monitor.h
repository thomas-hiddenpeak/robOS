/**
 * @file agx_monitor.h
 * @brief AGX Monitor Component for robOS
 *
 * This component provides real-time monitoring of AGX system status through
 * WebSocket connection to AGX server. It retrieves CPU, memory, temperature,
 * power and GPU data from tegrastats and makes it available to other robOS
 * components.
 *
 * Features:
 * - WebSocket connection to AGX server using Socket.IO protocol
 * - Real-time tegrastats data reception and parsing
 * - Automatic reconnection with fixed interval strategy
 * - Thread-safe data access with mutex protection
 * - Event callback system for data updates
 * - Console interface for debugging and status monitoring
 * - Integration with robOS config_manager and event_manager
 *
 * WebSocket Configuration:
 * - Default URL: ws://10.10.99.98:58090/socket.io/
 * - Protocol: Socket.IO over WebSocket
 * - Event: tegrastats_update
 * - Data format: JSON with CPU, memory, temperature, power, GPU info
 * - Update frequency: 1Hz (configurable on server side)
 *
 * @version 1.0.0
 * @date 2025-10-04
 * @author robOS Team
 */

#ifndef AGX_MONITOR_H
#define AGX_MONITOR_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Macros
 * ============================================================================
 */

#define AGX_MONITOR_VERSION "1.0.0"
#define AGX_MONITOR_MAX_URL_LENGTH (128)      ///< Maximum URL length
#define AGX_MONITOR_MAX_ERROR_MSG_LENGTH (64) ///< Maximum error message length
#define AGX_MONITOR_MAX_TIMESTAMP_LENGTH (32) ///< Maximum timestamp length
#define AGX_MONITOR_MAX_CPU_CORES (16)        ///< Maximum CPU cores supported
#define AGX_MONITOR_DEFAULT_TASK_STACK_SIZE (8192) ///< Default task stack size
#define AGX_MONITOR_DEFAULT_TASK_PRIORITY (5)      ///< Default task priority

/* Default configuration values */
#define AGX_MONITOR_DEFAULT_SERVER_URL "10.10.99.98"
#define AGX_MONITOR_DEFAULT_SERVER_PORT (58090)
#define AGX_MONITOR_DEFAULT_RECONNECT_INTERVAL_MS (3000)
#define AGX_MONITOR_DEFAULT_FAST_RETRY_COUNT (3)
#define AGX_MONITOR_DEFAULT_FAST_RETRY_INTERVAL_MS (1000)
#define AGX_MONITOR_DEFAULT_HEARTBEAT_TIMEOUT_MS (10000)
#define AGX_MONITOR_DEFAULT_STARTUP_DELAY_MS                                   \
  (45000) // AGX needs 45 seconds to boot

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * @brief AGX monitor error codes
 */
typedef enum {
  AGX_MONITOR_ERR_OK = 0,           ///< Success
  AGX_MONITOR_ERR_INVALID_ARG,      ///< Invalid argument
  AGX_MONITOR_ERR_INVALID_STATE,    ///< Invalid state
  AGX_MONITOR_ERR_NO_MEM,           ///< Out of memory
  AGX_MONITOR_ERR_CONNECT_FAILED,   ///< Connection failed
  AGX_MONITOR_ERR_PARSE_FAILED,     ///< Data parsing failed
  AGX_MONITOR_ERR_TIMEOUT,          ///< Operation timeout
  AGX_MONITOR_ERR_NOT_INITIALIZED,  ///< Component not initialized
  AGX_MONITOR_ERR_ALREADY_RUNNING,  ///< Component already running
  AGX_MONITOR_ERR_WEBSOCKET_ERROR,  ///< WebSocket error
  AGX_MONITOR_ERR_JSON_PARSE_ERROR, ///< JSON parsing error
  AGX_MONITOR_ERR_NETWORK_ERROR     ///< Network error
} agx_monitor_err_t;

/**
 * @brief AGX monitor connection status
 */
typedef enum {
  AGX_MONITOR_STATUS_UNINITIALIZED = 0, ///< Component not initialized
  AGX_MONITOR_STATUS_INITIALIZED,       ///< Initialized but not started
  AGX_MONITOR_STATUS_CONNECTING,        ///< Attempting to connect
  AGX_MONITOR_STATUS_CONNECTED,         ///< Connected and receiving data
  AGX_MONITOR_STATUS_DISCONNECTED,      ///< Disconnected
  AGX_MONITOR_STATUS_RECONNECTING,      ///< Attempting to reconnect
  AGX_MONITOR_STATUS_ERROR              ///< Error state
} agx_monitor_status_t;

/**
 * @brief AGX monitor event types
 */
typedef enum {
  AGX_MONITOR_EVENT_CONNECTED,     ///< Successfully connected
  AGX_MONITOR_EVENT_DISCONNECTED,  ///< Connection lost
  AGX_MONITOR_EVENT_DATA_RECEIVED, ///< New data received
  AGX_MONITOR_EVENT_ERROR,         ///< Error occurred
  AGX_MONITOR_EVENT_RECONNECTING   ///< Attempting to reconnect
} agx_monitor_event_type_t;

/**
 * @brief CPU core information structure
 */
typedef struct {
  uint8_t id;    ///< Core ID
  uint8_t usage; ///< Usage percentage (0-100)
  uint16_t freq; ///< Frequency in MHz
} agx_cpu_core_t;

/**
 * @brief Memory information structure
 */
typedef struct {
  uint32_t used;   ///< Used memory in MB
  uint32_t total;  ///< Total memory in MB
  uint32_t cached; ///< Cached memory in MB (for swap)
  char unit[4];    ///< Unit string ("MB")
} agx_memory_info_t;

/**
 * @brief Power information structure
 */
typedef struct {
  uint32_t current; ///< Current power consumption in mW
  uint32_t average; ///< Average power consumption in mW
  char unit[4];     ///< Unit string ("mW")
} agx_power_info_t;

/**
 * @brief Complete AGX monitoring data structure
 */
typedef struct {
  char timestamp[AGX_MONITOR_MAX_TIMESTAMP_LENGTH]; ///< ISO 8601 timestamp

  // CPU information
  struct {
    uint8_t core_count;                              ///< Number of CPU cores
    agx_cpu_core_t cores[AGX_MONITOR_MAX_CPU_CORES]; ///< CPU core data
  } cpu;

  // Memory information
  struct {
    agx_memory_info_t ram;  ///< RAM information
    agx_memory_info_t swap; ///< SWAP information
  } memory;

  // Temperature information (in Celsius)
  struct {
    float cpu;  ///< CPU temperature
    float soc0; ///< SoC sensor 0 temperature
    float soc1; ///< SoC sensor 1 temperature
    float soc2; ///< SoC sensor 2 temperature
    float tj;   ///< Junction temperature
  } temperature;

  // Power information
  struct {
    agx_power_info_t gpu_soc; ///< GPU+SoC power consumption
    agx_power_info_t cpu_cv;  ///< CPU power consumption
    agx_power_info_t sys_5v;  ///< System 5V power consumption
    agx_power_info_t ram;     ///< RAM power consumption
    agx_power_info_t swap;    ///< Swap power consumption
  } power;

  // GPU information
  struct {
    uint8_t gr3d_freq; ///< 3D GPU frequency percentage
  } gpu;

  bool is_valid;           ///< Data validity flag
  uint64_t update_time_us; ///< Update timestamp in microseconds
} agx_monitor_data_t;

/**
 * @brief AGX monitor configuration structure
 */
typedef struct {
  char server_url[AGX_MONITOR_MAX_URL_LENGTH]; ///< WebSocket server URL
  uint16_t server_port;                        ///< Server port number
  uint32_t reconnect_interval_ms;              ///< Fixed reconnection interval
  uint32_t fast_retry_count;       ///< Number of fast retry attempts
  uint32_t fast_retry_interval_ms; ///< Fast retry interval
  uint32_t heartbeat_timeout_ms;   ///< Heartbeat timeout
  bool enable_ssl;                 ///< Enable SSL/TLS
  bool auto_start;                 ///< Auto start monitoring
  uint32_t startup_delay_ms; ///< Startup delay before first connection attempt
  uint32_t task_stack_size;  ///< Task stack size
  uint8_t task_priority;     ///< Task priority
} agx_monitor_config_t;

/**
 * @brief AGX monitor status information structure
 */
typedef struct {
  bool initialized;                       ///< Initialization status
  agx_monitor_status_t connection_status; ///< Current connection status
  bool running;                           ///< Running status
  uint32_t total_reconnects;              ///< Total reconnection attempts
  uint32_t messages_received;             ///< Total messages received
  uint32_t parse_errors;                  ///< Total parsing errors
  uint64_t last_message_time_us;          ///< Last message timestamp
  uint64_t uptime_ms;                     ///< Component uptime
  uint64_t connected_time_ms;             ///< Total connected time
  float connection_reliability;           ///< Connection reliability percentage
  char last_error[AGX_MONITOR_MAX_ERROR_MSG_LENGTH]; ///< Last error message
} agx_monitor_status_info_t;

/**
 * @brief Event callback function type
 *
 * @param event_type Type of event that occurred
 * @param event_data Event-specific data (can be NULL)
 * @param user_data User-provided data from callback registration
 */
typedef void (*agx_monitor_event_callback_t)(
    agx_monitor_event_type_t event_type, void *event_data, void *user_data);

/* ============================================================================
 * Public Functions
 * ============================================================================
 */

/**
 * @brief Get default configuration for AGX monitor
 *
 * Fills the provided configuration structure with default values.
 *
 * @param config Pointer to configuration structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_get_default_config(agx_monitor_config_t *config);

/**
 * @brief Initialize the AGX monitor component
 *
 * Initializes the component with the provided configuration. This must be
 * called before any other AGX monitor functions.
 *
 * @param config Configuration structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_init(const agx_monitor_config_t *config);

/**
 * @brief Deinitialize the AGX monitor component
 *
 * Stops the monitoring task, closes connections, and frees all resources.
 *
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_deinit(void);

/**
 * @brief Start AGX monitoring
 *
 * Starts the WebSocket connection and monitoring task. The component must
 * be initialized before calling this function.
 *
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_start(void);

/**
 * @brief Stop AGX monitoring
 *
 * Stops the monitoring task and closes the WebSocket connection, but keeps
 * the component initialized.
 *
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_stop(void);

/**
 * @brief Check if AGX monitor is initialized
 *
 * @return true if initialized, false otherwise
 */
bool agx_monitor_is_initialized(void);

/**
 * @brief Check if AGX monitor is running
 *
 * @return true if monitoring is active, false otherwise
 */
bool agx_monitor_is_running(void);

/**
 * @brief Get component status information
 *
 * Retrieves detailed status information about the AGX monitor component.
 *
 * @param status Pointer to status structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_get_status(agx_monitor_status_info_t *status);

/**
 * @brief Get latest AGX monitoring data
 *
 * Retrieves the most recent monitoring data received from the AGX server.
 * This function is thread-safe and can be called from any task.
 *
 * @param data Pointer to data structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_get_latest_data(agx_monitor_data_t *data);

/**
 * @brief Check if monitoring data is valid
 *
 * Checks if the latest monitoring data is valid and recent.
 *
 * @return true if data is valid, false otherwise
 */
bool agx_monitor_is_data_valid(void);

/**
 * @brief Get timestamp of last data update
 *
 * @return uint64_t Timestamp in microseconds, 0 if no data received yet
 */
uint64_t agx_monitor_get_last_update_time(void);

/**
 * @brief Register event callback
 *
 * Registers a callback function to be called when AGX monitor events occur.
 * Only one callback can be registered at a time.
 *
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_register_callback(agx_monitor_event_callback_t callback,
                                        void *user_data);

/**
 * @brief Unregister event callback
 *
 * Removes the currently registered event callback.
 *
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_unregister_callback(void);

/**
 * @brief Register console commands
 *
 * Registers AGX monitor console commands with the console core component.
 * This should be called after both agx_monitor and console_core are
 * initialized.
 *
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t agx_monitor_register_console_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* AGX_MONITOR_H */