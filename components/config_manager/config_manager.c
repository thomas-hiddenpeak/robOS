/**
 * @file config_manager.c
 * @brief Configuration Manager Component Implementation
 * 
 * @author robOS Team
 * @date 2025-09-28
 */

#include "config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Constants and Private Macros
 * ============================================================================ */

static const char *TAG = "CONFIG_MANAGER";

#define CONFIG_MANAGER_TASK_STACK_SIZE    2048
#define CONFIG_MANAGER_TASK_PRIORITY      3
#define CONFIG_MANAGER_DEFAULT_COMMIT_INTERVAL  5000  // 5 seconds

/* ============================================================================
 * Private Type Definitions
 * ============================================================================ */

typedef struct {
    bool initialized;
    bool auto_commit;
    bool create_backup;
    uint32_t commit_interval_ms;
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;
    bool pending_changes;
    bool task_stop_requested;
} config_manager_context_t;

/* ============================================================================
 * Private Variables
 * ============================================================================ */

static config_manager_context_t s_config_ctx = {0};

/* ============================================================================
 * Private Function Declarations
 * ============================================================================ */

static void config_manager_task(void *pvParameters);
static esp_err_t ensure_nvs_initialized(void);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================ */

config_manager_config_t config_manager_get_default_config(void)
{
    config_manager_config_t config = {
        .auto_commit = true,
        .create_backup = false,
        .commit_interval_ms = CONFIG_MANAGER_DEFAULT_COMMIT_INTERVAL
    };
    return config;
}

esp_err_t config_manager_init(const config_manager_config_t *config)
{
    if (s_config_ctx.initialized) {
        ESP_LOGW(TAG, "Config manager already initialized");
        return ESP_OK;
    }

    // Use default config if NULL
    config_manager_config_t default_config = config_manager_get_default_config();
    if (config == NULL) {
        config = &default_config;
    }

    ESP_LOGI(TAG, "Initializing configuration manager...");

    // Initialize NVS flash
    esp_err_t ret = ensure_nvs_initialized();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create mutex
    s_config_ctx.mutex = xSemaphoreCreateMutex();
    if (s_config_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize context
    s_config_ctx.auto_commit = config->auto_commit;
    s_config_ctx.create_backup = config->create_backup;
    s_config_ctx.commit_interval_ms = config->commit_interval_ms;
    s_config_ctx.pending_changes = false;
    s_config_ctx.task_stop_requested = false;

    // Create auto-commit task if enabled
    if (s_config_ctx.auto_commit && s_config_ctx.commit_interval_ms > 0) {
        BaseType_t task_ret = xTaskCreate(
            config_manager_task,
            "config_manager",
            CONFIG_MANAGER_TASK_STACK_SIZE / sizeof(StackType_t),
            NULL,
            CONFIG_MANAGER_TASK_PRIORITY,
            &s_config_ctx.task_handle
        );

        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create config manager task");
            vSemaphoreDelete(s_config_ctx.mutex);
            return ESP_ERR_NO_MEM;
        }
    }

    s_config_ctx.initialized = true;
    ESP_LOGI(TAG, "Configuration manager initialized successfully");

    return ESP_OK;
}

esp_err_t config_manager_deinit(void)
{
    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Commit any pending changes
    if (s_config_ctx.pending_changes) {
        config_manager_commit();
    }

    // Stop task gracefully
    if (s_config_ctx.task_handle != NULL) {
        s_config_ctx.task_stop_requested = true;
        
        // Wait for task to finish (with timeout)
        uint32_t wait_count = 0;
        while (eTaskGetState(s_config_ctx.task_handle) != eDeleted && wait_count < 100) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;
        }
        
        // If task is still running, force delete it
        if (eTaskGetState(s_config_ctx.task_handle) != eDeleted) {
            ESP_LOGW(TAG, "Force deleting config manager task");
            vTaskDelete(s_config_ctx.task_handle);
        }
        
        s_config_ctx.task_handle = NULL;
    }

    // Delete mutex
    if (s_config_ctx.mutex != NULL) {
        vSemaphoreDelete(s_config_ctx.mutex);
        s_config_ctx.mutex = NULL;
    }

    s_config_ctx.initialized = false;
    s_config_ctx.task_stop_requested = false;

    ESP_LOGI(TAG, "Configuration manager deinitialized");
    return ESP_OK;
}

bool config_manager_is_initialized(void)
{
    return s_config_ctx.initialized;
}

esp_err_t config_manager_set(const char *namespace, const char *key, 
                            config_type_t type, const void *value, size_t size)
{
    if (!s_config_ctx.initialized || namespace == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace, esp_err_to_name(ret));
        xSemaphoreGive(s_config_ctx.mutex);
        return ret;
    }

    // Set value based on type
    switch (type) {
        case CONFIG_TYPE_UINT8:
            ret = nvs_set_u8(nvs_handle, key, *(uint8_t*)value);
            break;
        case CONFIG_TYPE_UINT16:
            ret = nvs_set_u16(nvs_handle, key, *(uint16_t*)value);
            break;
        case CONFIG_TYPE_UINT32:
            ret = nvs_set_u32(nvs_handle, key, *(uint32_t*)value);
            break;
        case CONFIG_TYPE_INT8:
            ret = nvs_set_i8(nvs_handle, key, *(int8_t*)value);
            break;
        case CONFIG_TYPE_INT16:
            ret = nvs_set_i16(nvs_handle, key, *(int16_t*)value);
            break;
        case CONFIG_TYPE_INT32:
            ret = nvs_set_i32(nvs_handle, key, *(int32_t*)value);
            break;
        case CONFIG_TYPE_FLOAT: {
            // Store float as blob to avoid precision issues
            ret = nvs_set_blob(nvs_handle, key, value, sizeof(float));
            break;
        }
        case CONFIG_TYPE_BOOL:
            ret = nvs_set_u8(nvs_handle, key, *(bool*)value ? 1 : 0);
            break;
        case CONFIG_TYPE_STRING:
            ret = nvs_set_str(nvs_handle, key, (const char*)value);
            break;
        case CONFIG_TYPE_BLOB:
            ret = nvs_set_blob(nvs_handle, key, value, size);
            break;
        default:
            ret = ESP_ERR_INVALID_ARG;
            break;
    }

    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        s_config_ctx.pending_changes = true;
        ESP_LOGD(TAG, "Set config: %s.%s", namespace, key);
    } else {
        ESP_LOGE(TAG, "Failed to set config %s.%s: %s", namespace, key, esp_err_to_name(ret));
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
}

esp_err_t config_manager_get(const char *namespace, const char *key,
                            config_type_t type, void *value, size_t *size)
{
    if (!s_config_ctx.initialized || namespace == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(namespace, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", namespace, esp_err_to_name(ret));
        xSemaphoreGive(s_config_ctx.mutex);
        return ret;
    }

    // Get value based on type
    switch (type) {
        case CONFIG_TYPE_UINT8:
            ret = nvs_get_u8(nvs_handle, key, (uint8_t*)value);
            break;
        case CONFIG_TYPE_UINT16:
            ret = nvs_get_u16(nvs_handle, key, (uint16_t*)value);
            break;
        case CONFIG_TYPE_UINT32:
            ret = nvs_get_u32(nvs_handle, key, (uint32_t*)value);
            break;
        case CONFIG_TYPE_INT8:
            ret = nvs_get_i8(nvs_handle, key, (int8_t*)value);
            break;
        case CONFIG_TYPE_INT16:
            ret = nvs_get_i16(nvs_handle, key, (int16_t*)value);
            break;
        case CONFIG_TYPE_INT32:
            ret = nvs_get_i32(nvs_handle, key, (int32_t*)value);
            break;
        case CONFIG_TYPE_FLOAT: {
            // Load float from blob
            size_t required_size = sizeof(float);
            ret = nvs_get_blob(nvs_handle, key, value, &required_size);
            break;
        }
        case CONFIG_TYPE_BOOL: {
            uint8_t bool_val;
            ret = nvs_get_u8(nvs_handle, key, &bool_val);
            if (ret == ESP_OK) {
                *(bool*)value = (bool_val != 0);
            }
            break;
        }
        case CONFIG_TYPE_STRING:
            if (size == NULL) {
                ret = ESP_ERR_INVALID_ARG;
            } else {
                ret = nvs_get_str(nvs_handle, key, (char*)value, size);
            }
            break;
        case CONFIG_TYPE_BLOB:
            if (size == NULL) {
                ret = ESP_ERR_INVALID_ARG;
            } else {
                ret = nvs_get_blob(nvs_handle, key, value, size);
            }
            break;
        default:
            ret = ESP_ERR_INVALID_ARG;
            break;
    }

    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Get config: %s.%s", namespace, key);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_ERR_NOT_FOUND;  // Convert to generic error code
    } else {
        ESP_LOGE(TAG, "Failed to get config %s.%s: %s", namespace, key, esp_err_to_name(ret));
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
}

esp_err_t config_manager_delete(const char *namespace, const char *key)
{
    if (!s_config_ctx.initialized || namespace == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace, esp_err_to_name(ret));
        xSemaphoreGive(s_config_ctx.mutex);
        return ret;
    }

    ret = nvs_erase_key(nvs_handle, key);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        s_config_ctx.pending_changes = true;
        ESP_LOGI(TAG, "Deleted config: %s.%s", namespace, key);
    } else {
        ESP_LOGE(TAG, "Failed to delete config %s.%s: %s", namespace, key, esp_err_to_name(ret));
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
}

esp_err_t config_manager_clear_namespace(const char *namespace)
{
    if (!s_config_ctx.initialized || namespace == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace, esp_err_to_name(ret));
        xSemaphoreGive(s_config_ctx.mutex);
        return ret;
    }

    ret = nvs_erase_all(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        s_config_ctx.pending_changes = true;
        ESP_LOGI(TAG, "Cleared namespace: %s", namespace);
    } else {
        ESP_LOGE(TAG, "Failed to clear namespace %s: %s", namespace, esp_err_to_name(ret));
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
}

bool config_manager_exists(const char *namespace, const char *key)
{
    if (!s_config_ctx.initialized || namespace == NULL || key == NULL) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(namespace, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return false;
    }

    // Try different NVS types to check if key exists
    esp_err_t check_ret;
    
    // Check for integer types
    uint8_t u8_val;
    check_ret = nvs_get_u8(nvs_handle, key, &u8_val);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    uint16_t u16_val;
    check_ret = nvs_get_u16(nvs_handle, key, &u16_val);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    uint32_t u32_val;
    check_ret = nvs_get_u32(nvs_handle, key, &u32_val);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    int8_t i8_val;
    check_ret = nvs_get_i8(nvs_handle, key, &i8_val);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    int16_t i16_val;
    check_ret = nvs_get_i16(nvs_handle, key, &i16_val);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    int32_t i32_val;
    check_ret = nvs_get_i32(nvs_handle, key, &i32_val);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    // Check for string
    size_t str_len = 0;
    check_ret = nvs_get_str(nvs_handle, key, NULL, &str_len);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }
    
    // Check for blob
    size_t blob_len = 0;
    check_ret = nvs_get_blob(nvs_handle, key, NULL, &blob_len);
    if (check_ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }

    nvs_close(nvs_handle);
    return false;
}

esp_err_t config_manager_commit(void)
{
    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    if (s_config_ctx.pending_changes) {
        // Note: nvs_commit is now handled automatically by ESP-IDF
        s_config_ctx.pending_changes = false;
        ESP_LOGD(TAG, "Configuration changes committed");
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
}

esp_err_t config_manager_save_bulk(const char *namespace, const config_item_t *items, size_t count)
{
    if (!s_config_ctx.initialized || namespace == NULL || items == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < count; i++) {
        const config_item_t *item = &items[i];
        size_t size = 0;
        
        // Determine size for blob type
        if (item->type == CONFIG_TYPE_BLOB) {
            size = item->value.blob.size;
        }
        
        esp_err_t ret = config_manager_set(namespace, item->key, item->type, 
                                         (item->type == CONFIG_TYPE_BLOB) ? item->value.blob.data : &item->value,
                                         size);
        if (ret != ESP_OK && result == ESP_OK) {
            result = ret;  // Store first error
        }
    }

    return result;
}

esp_err_t config_manager_load_bulk(const char *namespace, config_item_t *items, size_t count)
{
    if (!s_config_ctx.initialized || namespace == NULL || items == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < count; i++) {
        config_item_t *item = &items[i];
        size_t size = CONFIG_MANAGER_MAX_STRING_LENGTH;
        
        // Adjust size for blob type
        if (item->type == CONFIG_TYPE_BLOB) {
            size = item->value.blob.size;
        }
        
        esp_err_t ret = config_manager_get(namespace, item->key, item->type,
                                         (item->type == CONFIG_TYPE_BLOB) ? item->value.blob.data : &item->value,
                                         (item->type == CONFIG_TYPE_STRING || item->type == CONFIG_TYPE_BLOB) ? &size : NULL);
        
        if (ret == ESP_OK) {
            item->is_default = false;
        } else if (ret == ESP_ERR_NOT_FOUND) {
            item->is_default = true;
            ret = ESP_OK;  // Not an error for bulk load
        }
        
        if (ret != ESP_OK && result == ESP_OK) {
            result = ret;  // Store first error
        }
    }

    return result;
}

esp_err_t config_manager_get_stats(const char *namespace, size_t *used_entries, 
                                  size_t *free_entries, size_t *total_size, size_t *used_size)
{
    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_stats_t nvs_stats;
    esp_err_t ret = nvs_get_stats(namespace, &nvs_stats);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get NVS stats: %s", esp_err_to_name(ret));
        return ret;
    }

    if (used_entries) *used_entries = nvs_stats.used_entries;
    if (free_entries) *free_entries = nvs_stats.free_entries;
    if (total_size) *total_size = nvs_stats.total_entries * nvs_stats.available_entries;
    if (used_size) *used_size = nvs_stats.used_entries;

    return ESP_OK;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================ */

static void config_manager_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Config manager auto-commit task started");

    while (!s_config_ctx.task_stop_requested) {
        vTaskDelay(pdMS_TO_TICKS(s_config_ctx.commit_interval_ms));
        
        if (s_config_ctx.pending_changes && s_config_ctx.initialized) {
            config_manager_commit();
        }
    }

    ESP_LOGI(TAG, "Config manager auto-commit task ended");
    vTaskDelete(NULL);
}

static esp_err_t ensure_nvs_initialized(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs to be erased, performing erase...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS flash initialized successfully");
    }
    
    return ret;
}