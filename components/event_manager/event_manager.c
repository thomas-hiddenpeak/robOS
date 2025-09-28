/**
 * @file event_manager.c
 * @brief Event Manager Component Implementation
 * 
 * @author robOS Team
 * @date 2025
 */

#include "event_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "EVENT_MANAGER";

// Define the event manager's own event base
ESP_EVENT_DEFINE_BASE(EVENT_MANAGER_EVENTS);

/**
 * @brief Event statistics entry
 */
typedef struct event_stats_entry {
    esp_event_base_t event_base;
    int32_t event_id;
    uint32_t send_count;
    uint32_t handler_count;
    uint64_t last_sent_time;
    struct event_stats_entry *next;
} event_stats_entry_t;

/**
 * @brief Event Manager internal state
 */
typedef struct {
    bool initialized;
    bool running;
    event_manager_config_t config;
    esp_event_loop_handle_t event_loop;
    SemaphoreHandle_t mutex;
    
    // Statistics
    uint32_t total_events_sent;
    uint32_t total_events_received;
    uint32_t active_handlers;
    uint32_t registered_bases;
    event_stats_entry_t *stats_list;
    
    // Logging
    bool logging_enabled;
} event_manager_state_t;

static event_manager_state_t s_event_manager = {
    .initialized = false,
    .running = false,
    .event_loop = NULL,
    .mutex = NULL,
    .total_events_sent = 0,
    .total_events_received = 0,
    .active_handlers = 0,
    .registered_bases = 0,
    .stats_list = NULL,
    .logging_enabled = false
};

/**
 * @brief Internal event handler wrapper
 */
static void event_handler_wrapper(void *handler_args, esp_event_base_t event_base, 
                                 int32_t event_id, void *event_data)
{
    if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_event_manager.total_events_received++;
        xSemaphoreGive(s_event_manager.mutex);
    }
    
    if (s_event_manager.logging_enabled) {
        ESP_LOGI(TAG, "Event received - Base: %s, ID: %" PRId32, event_base, event_id);
    }
    
    // Call the actual handler
    event_manager_handler_t actual_handler = (event_manager_handler_t)handler_args;
    if (actual_handler) {
        actual_handler(handler_args, event_base, event_id, event_data);
    }
}

/**
 * @brief Update event statistics
 */
static void update_event_stats(esp_event_base_t event_base, int32_t event_id)
{
    if (!s_event_manager.config.enable_statistics) {
        return;
    }
    
    if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    // Find existing entry or create new one
    event_stats_entry_t *entry = s_event_manager.stats_list;
    while (entry) {
        if (entry->event_base == event_base && entry->event_id == event_id) {
            break;
        }
        entry = entry->next;
    }
    
    if (!entry) {
        // Create new entry
        entry = malloc(sizeof(event_stats_entry_t));
        if (entry) {
            entry->event_base = event_base;
            entry->event_id = event_id;
            entry->send_count = 0;
            entry->handler_count = 1; // This will be updated separately
            entry->last_sent_time = 0;
            entry->next = s_event_manager.stats_list;
            s_event_manager.stats_list = entry;
        }
    }
    
    if (entry) {
        entry->send_count++;
        entry->last_sent_time = esp_timer_get_time();
    }
    
    xSemaphoreGive(s_event_manager.mutex);
}

event_manager_config_t event_manager_get_default_config(void)
{
    event_manager_config_t config = {
        .event_queue_size = 32,
        .event_task_stack_size = 4096,
        .event_task_priority = 5,
        .enable_statistics = true,
        .enable_logging = false
    };
    return config;
}

esp_err_t event_manager_init(const event_manager_config_t *config)
{
    if (s_event_manager.initialized) {
        ESP_LOGW(TAG, "Event manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing event manager...");
    
    // Use default config if none provided
    if (config) {
        s_event_manager.config = *config;
    } else {
        s_event_manager.config = event_manager_get_default_config();
    }
    
    // Create mutex
    s_event_manager.mutex = xSemaphoreCreateMutex();
    if (!s_event_manager.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Create event loop
    esp_event_loop_args_t loop_args = {
        .queue_size = s_event_manager.config.event_queue_size,
        .task_name = "event_mgr",
        .task_priority = s_event_manager.config.event_task_priority,
        .task_stack_size = s_event_manager.config.event_task_stack_size,
        .task_core_id = tskNO_AFFINITY
    };
    
    esp_err_t ret = esp_event_loop_create(&loop_args, &s_event_manager.event_loop);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_event_manager.mutex);
        return ret;
    }
    
    s_event_manager.logging_enabled = s_event_manager.config.enable_logging;
    s_event_manager.initialized = true;
    
    ESP_LOGI(TAG, "Event manager initialized successfully");
    
    return ESP_OK;
}

esp_err_t event_manager_deinit(void)
{
    if (!s_event_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing event manager...");
    
    // Stop if running
    if (s_event_manager.running) {
        event_manager_stop();
    }
    
    // Clean up event loop
    if (s_event_manager.event_loop) {
        esp_event_loop_delete(s_event_manager.event_loop);
        s_event_manager.event_loop = NULL;
    }
    
    // Clean up statistics
    if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        event_stats_entry_t *entry = s_event_manager.stats_list;
        while (entry) {
            event_stats_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
        s_event_manager.stats_list = NULL;
        xSemaphoreGive(s_event_manager.mutex);
    }
    
    // Clean up mutex
    if (s_event_manager.mutex) {
        vSemaphoreDelete(s_event_manager.mutex);
        s_event_manager.mutex = NULL;
    }
    
    // Reset state
    memset(&s_event_manager, 0, sizeof(s_event_manager));
    
    ESP_LOGI(TAG, "Event manager deinitialized");
    return ESP_OK;
}

esp_err_t event_manager_start(void)
{
    if (!s_event_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_event_manager.running) {
        return ESP_OK;
    }
    
    s_event_manager.running = true;
    ESP_LOGI(TAG, "Event manager started");
    
    return ESP_OK;
}

esp_err_t event_manager_stop(void)
{
    if (!s_event_manager.initialized || !s_event_manager.running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_event_manager.running = false;
    
    // Post stop event
    event_manager_post_event(EVENT_MANAGER_EVENTS, 
                            EVENT_MANAGER_EVENT_STOPPED, 
                            NULL, 0, 0);
    
    ESP_LOGI(TAG, "Event manager stopped");
    return ESP_OK;
}

esp_err_t event_manager_get_status(event_manager_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    status->initialized = s_event_manager.initialized;
    status->running = s_event_manager.running;
    status->total_events_sent = s_event_manager.total_events_sent;
    status->total_events_received = s_event_manager.total_events_received;
    status->active_handlers = s_event_manager.active_handlers;
    status->registered_bases = s_event_manager.registered_bases;
    
    xSemaphoreGive(s_event_manager.mutex);
    return ESP_OK;
}

bool event_manager_is_initialized(void)
{
    return s_event_manager.initialized;
}

bool event_manager_is_running(void)
{
    return s_event_manager.running;
}

esp_err_t event_manager_register_handler(esp_event_base_t event_base,
                                         int32_t event_id,
                                         event_manager_handler_t event_handler,
                                         void *event_handler_arg)
{
    if (!s_event_manager.initialized || !event_handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = esp_event_handler_register_with(s_event_manager.event_loop,
                                                   event_base,
                                                   event_id,
                                                   event_handler_wrapper,
                                                   (void *)event_handler);
    
    if (ret == ESP_OK) {
        if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_event_manager.active_handlers++;
            xSemaphoreGive(s_event_manager.mutex);
        }
        
        if (s_event_manager.logging_enabled) {
            ESP_LOGI(TAG, "Handler registered - Base: %s, ID: %" PRId32, event_base, event_id);
        }
        
        // Post handler added event
        event_manager_post_event(EVENT_MANAGER_EVENTS, 
                                EVENT_MANAGER_EVENT_HANDLER_ADDED, 
                                NULL, 0, 0);
    } else {
        ESP_LOGE(TAG, "Failed to register handler: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t event_manager_unregister_handler(esp_event_base_t event_base,
                                           int32_t event_id,
                                           event_manager_handler_t event_handler)
{
    if (!s_event_manager.initialized || !event_handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = esp_event_handler_unregister_with(s_event_manager.event_loop,
                                                     event_base,
                                                     event_id,
                                                     event_handler_wrapper);
    
    if (ret == ESP_OK) {
        if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_event_manager.active_handlers > 0) {
                s_event_manager.active_handlers--;
            }
            xSemaphoreGive(s_event_manager.mutex);
        }
        
        if (s_event_manager.logging_enabled) {
            ESP_LOGI(TAG, "Handler unregistered - Base: %s, ID: %" PRId32, event_base, event_id);
        }
        
        // Post handler removed event
        event_manager_post_event(EVENT_MANAGER_EVENTS, 
                                EVENT_MANAGER_EVENT_HANDLER_REMOVED, 
                                NULL, 0, 0);
    }
    
    return ret;
}

esp_err_t event_manager_post_event(esp_event_base_t event_base,
                                   int32_t event_id,
                                   const void *event_data,
                                   size_t event_data_size,
                                   uint32_t timeout_ms)
{
    if (!s_event_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TickType_t timeout_ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    esp_err_t ret = esp_event_post_to(s_event_manager.event_loop,
                                     event_base,
                                     event_id,
                                     event_data,
                                     event_data_size,
                                     timeout_ticks);
    
    if (ret == ESP_OK) {
        if (xSemaphoreTake(s_event_manager.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_event_manager.total_events_sent++;
            xSemaphoreGive(s_event_manager.mutex);
        }
        
        update_event_stats(event_base, event_id);
        
        if (s_event_manager.logging_enabled) {
            ESP_LOGI(TAG, "Event posted - Base: %s, ID: %" PRId32, event_base, event_id);
        }
    } else {
        ESP_LOGW(TAG, "Failed to post event: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t event_manager_set_logging(bool enable)
{
    s_event_manager.logging_enabled = enable;
    ESP_LOGI(TAG, "Event logging %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

void event_manager_print_status(void)
{
    event_manager_status_t status;
    if (event_manager_get_status(&status) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get status");
        return;
    }
    
    ESP_LOGI(TAG, "=== Event Manager Status ===");
    ESP_LOGI(TAG, "Initialized: %s", status.initialized ? "Yes" : "No");
    ESP_LOGI(TAG, "Running: %s", status.running ? "Yes" : "No");
    ESP_LOGI(TAG, "Events sent: %" PRIu32, status.total_events_sent);
    ESP_LOGI(TAG, "Events received: %" PRIu32, status.total_events_received);
    ESP_LOGI(TAG, "Active handlers: %" PRIu32, status.active_handlers);
    ESP_LOGI(TAG, "Registered bases: %" PRIu32, status.registered_bases);
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
}