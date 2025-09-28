/**
 * @file event_manager.h
 * @brief Event Manager Component API
 * 
 * This component provides a high-level wrapper around ESP-IDF's event system,
 * enabling asynchronous communication between robOS components.
 * 
 * Features:
 * - Centralized event registration and management
 * - Type-safe event data handling
 * - Event logging and debugging support
 * - Component lifecycle event tracking
 * - Performance monitoring and statistics
 * 
 * @author robOS Team
 * @date 2025
 */

#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of event handlers per event type
 */
#define EVENT_MANAGER_MAX_HANDLERS_PER_EVENT 10

/**
 * @brief Maximum number of registered event bases
 */
#define EVENT_MANAGER_MAX_EVENT_BASES 20

/**
 * @brief Event Manager Configuration
 */
typedef struct {
    size_t event_queue_size;        ///< Size of the event queue (default: 32)
    size_t event_task_stack_size;   ///< Stack size for event task (default: 4096)
    int event_task_priority;        ///< Priority of event task (default: 5)
    bool enable_statistics;         ///< Enable event statistics collection
    bool enable_logging;            ///< Enable event logging
} event_manager_config_t;

/**
 * @brief Event Manager Status
 */
typedef struct {
    bool initialized;               ///< Component initialized flag
    bool running;                   ///< Component running flag
    uint32_t total_events_sent;     ///< Total events sent
    uint32_t total_events_received; ///< Total events received
    uint32_t active_handlers;       ///< Number of active event handlers
    uint32_t registered_bases;      ///< Number of registered event bases
} event_manager_status_t;

/**
 * @brief Event Manager System Events
 */
typedef enum {
    EVENT_MANAGER_EVENT_STARTED,        ///< Event manager started
    EVENT_MANAGER_EVENT_STOPPED,        ///< Event manager stopped
    EVENT_MANAGER_EVENT_HANDLER_ADDED,  ///< New event handler registered
    EVENT_MANAGER_EVENT_HANDLER_REMOVED,///< Event handler unregistered
    EVENT_MANAGER_EVENT_ERROR,          ///< Error occurred
} event_manager_event_type_t;

/**
 * @brief Event handler function type
 * @param handler_args Handler arguments passed during registration
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
typedef void (*event_manager_handler_t)(void *handler_args, 
                                        esp_event_base_t event_base, 
                                        int32_t event_id, 
                                        void *event_data);

/**
 * @brief Event statistics structure
 */
typedef struct {
    esp_event_base_t event_base;    ///< Event base
    int32_t event_id;               ///< Event ID
    uint32_t send_count;            ///< Number of times this event was sent
    uint32_t handler_count;         ///< Number of handlers for this event
    uint64_t last_sent_time;        ///< Last time this event was sent (microseconds)
} event_manager_stats_t;

// Declare the event manager's own event base
ESP_EVENT_DECLARE_BASE(EVENT_MANAGER_EVENTS);

/**
 * @brief Get default configuration for event manager
 * @return Default configuration structure
 */
event_manager_config_t event_manager_get_default_config(void);

/**
 * @brief Initialize Event Manager
 * @param config Configuration parameters (NULL for default)
 * @return ESP_OK on success
 */
esp_err_t event_manager_init(const event_manager_config_t *config);

/**
 * @brief Deinitialize Event Manager
 * @return ESP_OK on success
 */
esp_err_t event_manager_deinit(void);

/**
 * @brief Start Event Manager
 * @return ESP_OK on success
 */
esp_err_t event_manager_start(void);

/**
 * @brief Stop Event Manager
 * @return ESP_OK on success
 */
esp_err_t event_manager_stop(void);

/**
 * @brief Configure Event Manager
 * @param config New configuration parameters
 * @return ESP_OK on success
 */
esp_err_t event_manager_configure(const event_manager_config_t *config);

/**
 * @brief Get current configuration
 * @param config Pointer to store current configuration
 * @return ESP_OK on success
 */
esp_err_t event_manager_get_config(event_manager_config_t *config);

/**
 * @brief Get Event Manager status
 * @param status Pointer to store status information
 * @return ESP_OK on success
 */
esp_err_t event_manager_get_status(event_manager_status_t *status);

/**
 * @brief Check if Event Manager is initialized
 * @return true if initialized, false otherwise
 */
bool event_manager_is_initialized(void);

/**
 * @brief Check if Event Manager is running
 * @return true if running, false otherwise
 */
bool event_manager_is_running(void);

/**
 * @brief Register an event handler
 * @param event_base Event base to register for
 * @param event_id Event ID to register for (ESP_EVENT_ANY_ID for all events)
 * @param event_handler Handler function
 * @param event_handler_arg Argument to pass to the handler
 * @return ESP_OK on success
 */
esp_err_t event_manager_register_handler(esp_event_base_t event_base,
                                         int32_t event_id,
                                         event_manager_handler_t event_handler,
                                         void *event_handler_arg);

/**
 * @brief Unregister an event handler
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_handler Handler function to unregister
 * @return ESP_OK on success
 */
esp_err_t event_manager_unregister_handler(esp_event_base_t event_base,
                                           int32_t event_id,
                                           event_manager_handler_t event_handler);

/**
 * @brief Post an event
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data (will be copied)
 * @param event_data_size Size of event data
 * @param timeout_ms Timeout in milliseconds (portMAX_DELAY for no timeout)
 * @return ESP_OK on success
 */
esp_err_t event_manager_post_event(esp_event_base_t event_base,
                                   int32_t event_id,
                                   const void *event_data,
                                   size_t event_data_size,
                                   uint32_t timeout_ms);

/**
 * @brief Post an event from ISR context
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data (will be copied)
 * @param event_data_size Size of event data
 * @param higher_priority_task_woken Set to pdTRUE if a higher priority task was woken
 * @return ESP_OK on success
 */
esp_err_t event_manager_post_event_isr(esp_event_base_t event_base,
                                       int32_t event_id,
                                       const void *event_data,
                                       size_t event_data_size,
                                       BaseType_t *higher_priority_task_woken);

/**
 * @brief Get event statistics
 * @param stats Array to store statistics
 * @param max_stats Maximum number of statistics entries
 * @param actual_stats Actual number of statistics entries returned
 * @return ESP_OK on success
 */
esp_err_t event_manager_get_statistics(event_manager_stats_t *stats,
                                       size_t max_stats,
                                       size_t *actual_stats);

/**
 * @brief Reset event statistics
 * @return ESP_OK on success
 */
esp_err_t event_manager_reset_statistics(void);

/**
 * @brief Enable or disable event logging
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t event_manager_set_logging(bool enable);

/**
 * @brief Print event manager status and statistics
 */
void event_manager_print_status(void);

#ifdef __cplusplus
}
#endif