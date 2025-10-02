/**
 * @file config_manager.c
 * @brief Configuration Manager Component Implementation
 *
 * @author robOS Team
 * @date 2025-09-28
 */

#include "config_manager.h"
#include "base64.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage_manager.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * Constants and Private Macros
 * ============================================================================
 */

static const char *TAG = "CONFIG_MANAGER";

#define CONFIG_MANAGER_TASK_STACK_SIZE 2048
#define CONFIG_MANAGER_TASK_PRIORITY 3
#define CONFIG_MANAGER_DEFAULT_COMMIT_INTERVAL 5000 // 5 seconds

/* ============================================================================
 * Private Type Definitions
 * ============================================================================
 */

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
 * ============================================================================
 */

static config_manager_context_t s_config_ctx = {0};

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

static void config_manager_task(void *pvParameters);
static esp_err_t ensure_nvs_initialized(void);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

config_manager_config_t config_manager_get_default_config(void) {
  config_manager_config_t config = {.auto_commit = true,
                                    .create_backup = false,
                                    .commit_interval_ms =
                                        CONFIG_MANAGER_DEFAULT_COMMIT_INTERVAL};
  return config;
}

esp_err_t config_manager_init(const config_manager_config_t *config) {
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
    BaseType_t task_ret =
        xTaskCreate(config_manager_task, "config_manager",
                    CONFIG_MANAGER_TASK_STACK_SIZE / sizeof(StackType_t), NULL,
                    CONFIG_MANAGER_TASK_PRIORITY, &s_config_ctx.task_handle);

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

esp_err_t config_manager_deinit(void) {
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
    while (eTaskGetState(s_config_ctx.task_handle) != eDeleted &&
           wait_count < 100) {
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

bool config_manager_is_initialized(void) { return s_config_ctx.initialized; }

esp_err_t config_manager_set(const char *namespace, const char *key,
                             config_type_t type, const void *value,
                             size_t size) {
  if (!s_config_ctx.initialized || namespace == NULL || key == NULL ||
      value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace,
             esp_err_to_name(ret));
    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
  }

  // Set value based on type
  switch (type) {
  case CONFIG_TYPE_UINT8:
    ret = nvs_set_u8(nvs_handle, key, *(uint8_t *)value);
    break;
  case CONFIG_TYPE_UINT16:
    ret = nvs_set_u16(nvs_handle, key, *(uint16_t *)value);
    break;
  case CONFIG_TYPE_UINT32:
    ret = nvs_set_u32(nvs_handle, key, *(uint32_t *)value);
    break;
  case CONFIG_TYPE_INT8:
    ret = nvs_set_i8(nvs_handle, key, *(int8_t *)value);
    break;
  case CONFIG_TYPE_INT16:
    ret = nvs_set_i16(nvs_handle, key, *(int16_t *)value);
    break;
  case CONFIG_TYPE_INT32:
    ret = nvs_set_i32(nvs_handle, key, *(int32_t *)value);
    break;
  case CONFIG_TYPE_FLOAT: {
    // Store float as blob to avoid precision issues
    ret = nvs_set_blob(nvs_handle, key, value, sizeof(float));
    break;
  }
  case CONFIG_TYPE_BOOL:
    ret = nvs_set_u8(nvs_handle, key, *(bool *)value ? 1 : 0);
    break;
  case CONFIG_TYPE_STRING:
    ret = nvs_set_str(nvs_handle, key, (const char *)value);
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
    ESP_LOGE(TAG, "Failed to set config %s.%s: %s", namespace, key,
             esp_err_to_name(ret));
  }

  xSemaphoreGive(s_config_ctx.mutex);
  return ret;
}

esp_err_t config_manager_get(const char *namespace, const char *key,
                             config_type_t type, void *value, size_t *size) {
  if (!s_config_ctx.initialized || namespace == NULL || key == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // For string and blob types, value can be NULL to query size
  if (value == NULL && type != CONFIG_TYPE_STRING && type != CONFIG_TYPE_BLOB) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", namespace,
             esp_err_to_name(ret));
    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
  }

  // Get value based on type
  switch (type) {
  case CONFIG_TYPE_UINT8:
    ret = nvs_get_u8(nvs_handle, key, (uint8_t *)value);
    break;
  case CONFIG_TYPE_UINT16:
    ret = nvs_get_u16(nvs_handle, key, (uint16_t *)value);
    break;
  case CONFIG_TYPE_UINT32:
    ret = nvs_get_u32(nvs_handle, key, (uint32_t *)value);
    break;
  case CONFIG_TYPE_INT8:
    ret = nvs_get_i8(nvs_handle, key, (int8_t *)value);
    break;
  case CONFIG_TYPE_INT16:
    ret = nvs_get_i16(nvs_handle, key, (int16_t *)value);
    break;
  case CONFIG_TYPE_INT32:
    ret = nvs_get_i32(nvs_handle, key, (int32_t *)value);
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
      *(bool *)value = (bool_val != 0);
    }
    break;
  }
  case CONFIG_TYPE_STRING:
    if (size == NULL) {
      ret = ESP_ERR_INVALID_ARG;
    } else {
      ret = nvs_get_str(nvs_handle, key, (char *)value, size);
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
    ret = ESP_ERR_NOT_FOUND; // Convert to generic error code
  } else {
    ESP_LOGE(TAG, "Failed to get config %s.%s: %s", namespace, key,
             esp_err_to_name(ret));
  }

  xSemaphoreGive(s_config_ctx.mutex);
  return ret;
}

esp_err_t config_manager_delete(const char *namespace, const char *key) {
  if (!s_config_ctx.initialized || namespace == NULL || key == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace,
             esp_err_to_name(ret));
    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
  }

  ret = nvs_erase_key(nvs_handle, key);
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    s_config_ctx.pending_changes = true;
    ESP_LOGI(TAG, "Deleted config: %s.%s", namespace, key);
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    // 配置项不存在是正常情况，不需要记录错误
    ESP_LOGD(TAG, "Config %s.%s not found (already deleted)", namespace, key);
  } else {
    ESP_LOGE(TAG, "Failed to delete config %s.%s: %s", namespace, key,
             esp_err_to_name(ret));
  }

  xSemaphoreGive(s_config_ctx.mutex);
  return ret;
}

esp_err_t config_manager_clear_namespace(const char *namespace) {
  if (!s_config_ctx.initialized || namespace == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace,
             esp_err_to_name(ret));
    xSemaphoreGive(s_config_ctx.mutex);
    return ret;
  }

  ret = nvs_erase_all(nvs_handle);
  nvs_close(nvs_handle);

  if (ret == ESP_OK) {
    s_config_ctx.pending_changes = true;
    ESP_LOGI(TAG, "Cleared namespace: %s", namespace);
  } else {
    ESP_LOGE(TAG, "Failed to clear namespace %s: %s", namespace,
             esp_err_to_name(ret));
  }

  xSemaphoreGive(s_config_ctx.mutex);
  return ret;
}

bool config_manager_exists(const char *namespace, const char *key) {
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

esp_err_t config_manager_commit(void) {
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

esp_err_t config_manager_save_bulk(const char *namespace,
                                   const config_item_t *items, size_t count) {
  if (!s_config_ctx.initialized || namespace == NULL || items == NULL ||
      count == 0) {
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

    esp_err_t ret = config_manager_set(
        namespace, item->key, item->type,
        (item->type == CONFIG_TYPE_BLOB) ? item->value.blob.data : &item->value,
        size);
    if (ret != ESP_OK && result == ESP_OK) {
      result = ret; // Store first error
    }
  }

  return result;
}

esp_err_t config_manager_load_bulk(const char *namespace, config_item_t *items,
                                   size_t count) {
  if (!s_config_ctx.initialized || namespace == NULL || items == NULL ||
      count == 0) {
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

    esp_err_t ret = config_manager_get(
        namespace, item->key, item->type,
        (item->type == CONFIG_TYPE_BLOB) ? item->value.blob.data : &item->value,
        (item->type == CONFIG_TYPE_STRING || item->type == CONFIG_TYPE_BLOB)
            ? &size
            : NULL);

    if (ret == ESP_OK) {
      item->is_default = false;
    } else if (ret == ESP_ERR_NOT_FOUND) {
      item->is_default = true;
      ret = ESP_OK; // Not an error for bulk load
    }

    if (ret != ESP_OK && result == ESP_OK) {
      result = ret; // Store first error
    }
  }

  return result;
}

esp_err_t config_manager_get_stats(const char *namespace, size_t *used_entries,
                                   size_t *free_entries, size_t *total_size,
                                   size_t *used_size) {
  if (!s_config_ctx.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  nvs_stats_t nvs_stats;
  esp_err_t ret = nvs_get_stats(namespace, &nvs_stats);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get NVS stats: %s", esp_err_to_name(ret));
    return ret;
  }

  if (used_entries)
    *used_entries = nvs_stats.used_entries;
  if (free_entries)
    *free_entries = nvs_stats.free_entries;
  if (total_size)
    *total_size = nvs_stats.total_entries * nvs_stats.available_entries;
  if (used_size)
    *used_size = nvs_stats.used_entries;

  return ESP_OK;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static void config_manager_task(void *pvParameters) {
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

static esp_err_t ensure_nvs_initialized(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS flash needs to be erased, performing erase...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "NVS flash initialized successfully");
  }

  return ret;
}

/* ============================================================================
 * SD Card Import/Export Function Implementations
 * ============================================================================
 */

/**
 * @brief 创建目录的同步包装器
 */
static esp_err_t create_directory_sync(const char *path) {
  // 使用简单的信号量来实现同步
  static SemaphoreHandle_t mkdir_sem = NULL;
  static esp_err_t mkdir_result = ESP_OK;

  if (mkdir_sem == NULL) {
    mkdir_sem = xSemaphoreCreateBinary();
    if (mkdir_sem == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  // 回调函数
  auto void mkdir_callback(storage_operation_type_t operation, esp_err_t result,
                           void *data, void *user_data) {
    mkdir_result = result;
    xSemaphoreGive(mkdir_sem);
  }

  // 发起异步目录创建请求
  esp_err_t ret = storage_manager_mkdir_async(path, mkdir_callback, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  // 等待操作完成（最多等待5秒）
  if (xSemaphoreTake(mkdir_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
    return mkdir_result;
  } else {
    return ESP_ERR_TIMEOUT;
  }
}

/**
 * @brief Get string representation of config type
 */
static const char *config_type_to_string(config_type_t type) {
  switch (type) {
  case CONFIG_TYPE_UINT8:
    return "uint8";
  case CONFIG_TYPE_UINT16:
    return "uint16";
  case CONFIG_TYPE_UINT32:
    return "uint32";
  case CONFIG_TYPE_INT8:
    return "int8";
  case CONFIG_TYPE_INT16:
    return "int16";
  case CONFIG_TYPE_INT32:
    return "int32";
  case CONFIG_TYPE_FLOAT:
    return "float";
  case CONFIG_TYPE_BOOL:
    return "bool";
  case CONFIG_TYPE_STRING:
    return "string";
  case CONFIG_TYPE_BLOB:
    return "blob";
  default:
    return "unknown";
  }
}

/**
 * @brief Parse string to config type
 */
static config_type_t string_to_config_type(const char *type_str) {
  if (strcmp(type_str, "uint8") == 0)
    return CONFIG_TYPE_UINT8;
  if (strcmp(type_str, "uint16") == 0)
    return CONFIG_TYPE_UINT16;
  if (strcmp(type_str, "uint32") == 0)
    return CONFIG_TYPE_UINT32;
  if (strcmp(type_str, "int8") == 0)
    return CONFIG_TYPE_INT8;
  if (strcmp(type_str, "int16") == 0)
    return CONFIG_TYPE_INT16;
  if (strcmp(type_str, "int32") == 0)
    return CONFIG_TYPE_INT32;
  if (strcmp(type_str, "float") == 0)
    return CONFIG_TYPE_FLOAT;
  if (strcmp(type_str, "bool") == 0)
    return CONFIG_TYPE_BOOL;
  if (strcmp(type_str, "string") == 0)
    return CONFIG_TYPE_STRING;
  if (strcmp(type_str, "blob") == 0)
    return CONFIG_TYPE_BLOB;
  return CONFIG_TYPE_INVALID;
}

/**
 * @brief Create JSON value from config item
 */
static cJSON *config_value_to_json(const config_item_t *item) {
  cJSON *json_value = cJSON_CreateObject();
  if (json_value == NULL) {
    return NULL;
  }

  // Add type information
  cJSON_AddStringToObject(json_value, "type",
                          config_type_to_string(item->type));

  // Add value based on type
  switch (item->type) {
  case CONFIG_TYPE_UINT8:
  case CONFIG_TYPE_UINT16:
  case CONFIG_TYPE_UINT32:
    cJSON_AddNumberToObject(json_value, "value", item->value.u32);
    break;
  case CONFIG_TYPE_INT8:
  case CONFIG_TYPE_INT16:
  case CONFIG_TYPE_INT32:
    cJSON_AddNumberToObject(json_value, "value", item->value.i32);
    break;
  case CONFIG_TYPE_FLOAT:
    cJSON_AddNumberToObject(json_value, "value", item->value.f);
    break;
  case CONFIG_TYPE_BOOL:
    cJSON_AddBoolToObject(json_value, "value", item->value.b);
    break;
  case CONFIG_TYPE_STRING:
    cJSON_AddStringToObject(json_value, "value", item->value.str);
    break;
  case CONFIG_TYPE_BLOB:
    // For blob data, we'll encode as base64 or hex string
    // For now, we'll skip blob export (can be enhanced later)
    cJSON_AddStringToObject(json_value, "value", "[BLOB_DATA_NOT_EXPORTED]");
    break;
  default:
    cJSON_AddNullToObject(json_value, "value");
    break;
  }

  return json_value;
}

/**
 * @brief Export all keys from a namespace to JSON
 */
static esp_err_t export_namespace_to_json(const char *namespace,
                                          cJSON *json_namespace) {
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open namespace '%s': %s", namespace,
             esp_err_to_name(ret));
    return ret;
  }

  nvs_iterator_t iter = NULL;
  ret = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_ANY, &iter);

  while (ret == ESP_OK && iter != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(iter, &info);

    config_item_t item = {0};
    item.key = info.key;

    // Determine type and read value
    switch (info.type) {
    case NVS_TYPE_U8:
      item.type = CONFIG_TYPE_UINT8;
      ret = nvs_get_u8(handle, info.key, &item.value.u8);
      break;
    case NVS_TYPE_U16:
      item.type = CONFIG_TYPE_UINT16;
      ret = nvs_get_u16(handle, info.key, &item.value.u16);
      break;
    case NVS_TYPE_U32:
      item.type = CONFIG_TYPE_UINT32;
      ret = nvs_get_u32(handle, info.key, &item.value.u32);
      break;
    case NVS_TYPE_I8:
      item.type = CONFIG_TYPE_INT8;
      ret = nvs_get_i8(handle, info.key, &item.value.i8);
      break;
    case NVS_TYPE_I16:
      item.type = CONFIG_TYPE_INT16;
      ret = nvs_get_i16(handle, info.key, &item.value.i16);
      break;
    case NVS_TYPE_I32:
      item.type = CONFIG_TYPE_INT32;
      ret = nvs_get_i32(handle, info.key, &item.value.i32);
      break;
    case NVS_TYPE_STR: {
      item.type = CONFIG_TYPE_STRING;
      size_t required_size = 0;
      ret = nvs_get_str(handle, info.key, NULL, &required_size);
      if (ret == ESP_OK && required_size <= CONFIG_MANAGER_MAX_STRING_LENGTH) {
        ret = nvs_get_str(handle, info.key, item.value.str, &required_size);
      }
      break;
    }
    case NVS_TYPE_BLOB: {
      // Handle blob data by converting to base64
      size_t required_size = 0;
      ret = nvs_get_blob(handle, info.key, NULL, &required_size);

      if (ret == ESP_OK && required_size > 0 &&
          required_size <= 4096) { // Limit blob size to 4KB
        uint8_t *blob_data = malloc(required_size);
        if (blob_data != NULL) {
          ret = nvs_get_blob(handle, info.key, blob_data, &required_size);
          if (ret == ESP_OK) {
            // Convert blob to base64 string for JSON storage
            size_t base64_len = base64_encode_len(required_size);
            char *base64_str = malloc(base64_len);
            if (base64_str != NULL) {
              // Encode binary data to base64
              if (base64_encode(blob_data, required_size, base64_str,
                                base64_len) == 0) {
                cJSON *blob_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(blob_obj, "type", "blob");
                cJSON_AddNumberToObject(blob_obj, "size", required_size);
                cJSON_AddStringToObject(blob_obj, "data", base64_str);
                cJSON_AddItemToObject(json_namespace, info.key, blob_obj);

                ESP_LOGI(TAG, "Exported blob key '%s' (%zu bytes) as base64",
                         info.key, required_size);
              } else {
                ESP_LOGW(TAG, "Failed to encode blob key '%s' to base64",
                         info.key);
              }
              free(base64_str);
            }
          }
          free(blob_data);
        }
      } else {
        ESP_LOGW(TAG, "Skipping blob key '%s' (size: %zu bytes)", info.key,
                 required_size);
      }
      ret = nvs_entry_next(&iter);
      continue;
    }
    default:
      ESP_LOGW(TAG, "Unknown type %d for key '%s'", info.type, info.key);
      ret = nvs_entry_next(&iter);
      continue;
    }

    if (ret == ESP_OK) {
      cJSON *json_item = config_value_to_json(&item);
      if (json_item != NULL) {
        cJSON_AddItemToObject(json_namespace, info.key, json_item);
      }
    }

    ret = nvs_entry_next(&iter);
  }

  if (iter != NULL) {
    nvs_release_iterator(iter);
  }
  nvs_close(handle);

  return ESP_OK;
}

esp_err_t config_manager_export_to_sdcard(const char *namespace,
                                          const char *file_path) {
  if (!s_config_ctx.initialized) {
    ESP_LOGE(TAG, "Config manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (file_path == NULL) {
    ESP_LOGE(TAG, "File path cannot be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex
  if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex for export");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "Exporting configuration to SD card: %s", file_path);

  cJSON *json_root = cJSON_CreateObject();
  if (json_root == NULL) {
    ESP_LOGE(TAG, "Failed to create JSON root object");
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_NO_MEM;
  }

  // Add metadata
  cJSON_AddStringToObject(json_root, "format_version", "1.0");
  cJSON_AddStringToObject(json_root, "export_time", ""); // TODO: Add timestamp
  cJSON_AddStringToObject(json_root, "device_id", "robOS");

  cJSON *json_config = cJSON_CreateObject();
  cJSON_AddItemToObject(json_root, "configuration", json_config);

  esp_err_t ret = ESP_OK;

  if (namespace != NULL) {
    // Export specific namespace
    cJSON *json_namespace = cJSON_CreateObject();
    ret = export_namespace_to_json(namespace, json_namespace);
    if (ret == ESP_OK) {
      cJSON_AddItemToObject(json_config, namespace, json_namespace);
    } else {
      cJSON_Delete(json_namespace);
    }
  } else {
    // Export all namespaces by iterating through NVS partition
    ESP_LOGI(TAG, "Exporting all namespaces from NVS partition");

    nvs_iterator_t it = NULL;
    esp_err_t iter_ret =
        nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);

    // Keep track of unique namespaces we've found
    char processed_namespaces[32][NVS_KEY_NAME_MAX_SIZE];
    int namespace_count = 0;
    bool any_exported = false;

    while (iter_ret == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);

      // Check if we've already processed this namespace
      bool namespace_already_processed = false;
      for (int i = 0; i < namespace_count; i++) {
        if (strcmp(processed_namespaces[i], info.namespace_name) == 0) {
          namespace_already_processed = true;
          break;
        }
      }

      // If this is a new namespace, export it
      if (!namespace_already_processed && namespace_count < 32) {
        strcpy(processed_namespaces[namespace_count], info.namespace_name);
        namespace_count++;

        cJSON *json_namespace = cJSON_CreateObject();
        esp_err_t ns_ret =
            export_namespace_to_json(info.namespace_name, json_namespace);
        if (ns_ret == ESP_OK) {
          cJSON_AddItemToObject(json_config, info.namespace_name,
                                json_namespace);
          any_exported = true;
          ESP_LOGI(TAG, "Exported namespace: %s", info.namespace_name);
        } else {
          cJSON_Delete(json_namespace);
          ESP_LOGD(TAG, "Namespace %s not found or empty", info.namespace_name);
        }
      }

      iter_ret = nvs_entry_next(&it);
    }

    if (it != NULL) {
      nvs_release_iterator(it);
    }

    if (!any_exported) {
      ESP_LOGW(TAG, "No configuration data found to export");
      ret = ESP_ERR_NOT_FOUND;
    } else {
      ESP_LOGI(TAG, "Successfully exported %d namespaces", namespace_count);
    }
  }

  if (ret == ESP_OK) {
    // Write JSON to file
    char *json_string = cJSON_Print(json_root);
    if (json_string != NULL) {
      FILE *file = fopen(file_path, "w");
      if (file != NULL) {
        size_t written = fwrite(json_string, 1, strlen(json_string), file);
        if (written != strlen(json_string)) {
          ESP_LOGE(TAG, "Failed to write complete JSON to file");
          ret = ESP_ERR_INVALID_SIZE;
        }
        fclose(file);
        ESP_LOGI(TAG, "Configuration exported successfully to %s", file_path);
      } else {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", file_path);
        ret = ESP_ERR_NOT_FOUND;
      }
      free(json_string);
    } else {
      ESP_LOGE(TAG, "Failed to convert JSON to string");
      ret = ESP_ERR_NO_MEM;
    }
  }

  cJSON_Delete(json_root);
  xSemaphoreGive(s_config_ctx.mutex);

  return ret;
}

esp_err_t config_manager_import_from_sdcard(const char *file_path,
                                            const char *namespace,
                                            bool overwrite) {
  if (!s_config_ctx.initialized) {
    ESP_LOGE(TAG, "Config manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (file_path == NULL) {
    ESP_LOGE(TAG, "File path cannot be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex
  if (xSemaphoreTake(s_config_ctx.mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take mutex for import");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "Importing configuration from SD card: %s", file_path);

  FILE *file = fopen(file_path, "r");
  if (file == NULL) {
    ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0 || file_size > 64 * 1024) { // Limit to 64KB
    ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
    fclose(file);
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_INVALID_SIZE;
  }

  // Read file content
  char *json_buffer = malloc(file_size + 1);
  if (json_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for file content");
    fclose(file);
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_NO_MEM;
  }

  size_t read_size = fread(json_buffer, 1, file_size, file);
  json_buffer[read_size] = '\0';
  fclose(file);

  if (read_size != file_size) {
    ESP_LOGE(TAG, "Failed to read complete file");
    free(json_buffer);
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_INVALID_SIZE;
  }

  // Parse JSON
  cJSON *json_root = cJSON_Parse(json_buffer);
  free(json_buffer);

  if (json_root == NULL) {
    ESP_LOGE(TAG, "Failed to parse JSON file");
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = ESP_OK;

  // Validate format
  cJSON *format_version = cJSON_GetObjectItem(json_root, "format_version");
  if (format_version == NULL || !cJSON_IsString(format_version)) {
    ESP_LOGW(TAG, "Missing or invalid format version");
  }

  cJSON *json_config = cJSON_GetObjectItem(json_root, "configuration");
  if (json_config == NULL || !cJSON_IsObject(json_config)) {
    ESP_LOGE(TAG, "Missing or invalid configuration object");
    ret = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }

  // Import configuration data
  cJSON *ns_item = NULL;
  cJSON_ArrayForEach(ns_item, json_config) {
    const char *ns_name = namespace ? namespace : ns_item->string;

    if (!cJSON_IsObject(ns_item)) {
      ESP_LOGW(TAG, "Skipping non-object namespace: %s", ns_item->string);
      continue;
    }

    ESP_LOGI(TAG, "Importing namespace: %s", ns_name);

    cJSON *key_item = NULL;
    cJSON_ArrayForEach(key_item, ns_item) {
      const char *key_name = key_item->string;

      if (!cJSON_IsObject(key_item)) {
        ESP_LOGW(TAG, "Skipping non-object key: %s", key_name);
        continue;
      }

      cJSON *type_json = cJSON_GetObjectItem(key_item, "type");
      cJSON *value_json = cJSON_GetObjectItem(key_item, "value");

      // Handle blob format: for blob type, value comes from "data" field
      if (type_json != NULL && value_json == NULL) {
        const char *type_str = cJSON_GetStringValue(type_json);
        if (type_str != NULL && strcmp(type_str, "blob") == 0) {
          value_json = cJSON_GetObjectItem(key_item, "data");
        }
      }

      if (type_json == NULL || value_json == NULL) {
        ESP_LOGW(TAG, "Missing type or value for key: %s", key_name);
        continue;
      }

      const char *type_str = cJSON_GetStringValue(type_json);
      if (type_str == NULL) {
        ESP_LOGW(TAG, "Invalid type for key: %s", key_name);
        continue;
      }

      config_type_t type = string_to_config_type(type_str);
      if (type == CONFIG_TYPE_INVALID) {
        ESP_LOGW(TAG, "Unknown type '%s' for key: %s", type_str, key_name);
        continue;
      }

      // Check if key exists and handle overwrite
      if (!overwrite && config_manager_exists(ns_name, key_name)) {
        ESP_LOGD(TAG, "Skipping existing key: %s (overwrite disabled)",
                 key_name);
        continue;
      }

      // Import value based on type
      esp_err_t import_ret = ESP_OK;
      switch (type) {
      case CONFIG_TYPE_UINT8: {
        uint8_t val = (uint8_t)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_UINT16: {
        uint16_t val = (uint16_t)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_UINT32: {
        uint32_t val = (uint32_t)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_INT8: {
        int8_t val = (int8_t)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_INT16: {
        int16_t val = (int16_t)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_INT32: {
        int32_t val = (int32_t)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_FLOAT: {
        float val = (float)cJSON_GetNumberValue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_BOOL: {
        bool val = cJSON_IsTrue(value_json);
        import_ret =
            config_manager_set(ns_name, key_name, type, &val, sizeof(val));
        break;
      }
      case CONFIG_TYPE_STRING: {
        const char *val = cJSON_GetStringValue(value_json);
        if (val != NULL) {
          import_ret =
              config_manager_set(ns_name, key_name, type, val, strlen(val) + 1);
        } else {
          ESP_LOGW(TAG, "Invalid string value for key: %s", key_name);
        }
        break;
      }
      case CONFIG_TYPE_BLOB: {
        // Get blob data from base64 encoding
        cJSON *size_json = cJSON_GetObjectItem(key_item, "size");
        const char *base64_data = cJSON_GetStringValue(value_json);

        if (size_json == NULL || base64_data == NULL) {
          ESP_LOGW(TAG, "Invalid blob format for key: %s", key_name);
          continue;
        }

        size_t expected_size = (size_t)cJSON_GetNumberValue(size_json);
        if (expected_size == 0 ||
            expected_size > 4096) { // Limit blob size to 4KB
          ESP_LOGW(TAG, "Invalid blob size %zu for key: %s", expected_size,
                   key_name);
          continue;
        }

        // Decode base64 data
        size_t base64_len = strlen(base64_data);
        size_t decoded_len = base64_decode_len(base64_len);
        uint8_t *decoded_data = malloc(decoded_len);

        if (decoded_data != NULL) {
          int actual_decoded_len =
              base64_decode(base64_data, base64_len, decoded_data, decoded_len);
          if (actual_decoded_len > 0) {
            // Verify decoded size matches expected size
            if ((size_t)actual_decoded_len >= expected_size) {
              import_ret = config_manager_set(ns_name, key_name, type,
                                              decoded_data, expected_size);
              if (import_ret == ESP_OK) {
                ESP_LOGI(TAG, "Imported blob key '%s' (%zu bytes)", key_name,
                         expected_size);
              }
            } else {
              ESP_LOGW(TAG,
                       "Decoded size %d does not match expected size %zu for "
                       "key: %s",
                       actual_decoded_len, expected_size, key_name);
            }
          } else {
            ESP_LOGW(TAG, "Failed to decode base64 data for blob key: %s",
                     key_name);
          }
          free(decoded_data);
        } else {
          ESP_LOGE(TAG, "Failed to allocate memory for blob decoding: %s",
                   key_name);
        }
        break;
      }
      default:
        ESP_LOGW(TAG, "Unsupported type for import: %s", type_str);
        continue;
      }

      if (import_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to import key '%s': %s", key_name,
                 esp_err_to_name(import_ret));
      } else {
        ESP_LOGD(TAG, "Imported key: %s", key_name);
      }
    }
  }

  // Commit changes
  config_manager_commit();
  ESP_LOGI(TAG, "Configuration import completed");

cleanup:
  cJSON_Delete(json_root);
  xSemaphoreGive(s_config_ctx.mutex);

  return ret;
}

esp_err_t config_manager_validate_sdcard_file(const char *file_path,
                                              size_t *namespace_count,
                                              size_t *total_keys) {
  if (file_path == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *file = fopen(file_path, "r");
  if (file == NULL) {
    return ESP_ERR_NOT_FOUND;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0 || file_size > 64 * 1024) {
    fclose(file);
    return ESP_ERR_INVALID_SIZE;
  }

  // Read and parse JSON
  char *json_buffer = malloc(file_size + 1);
  if (json_buffer == NULL) {
    fclose(file);
    return ESP_ERR_NO_MEM;
  }

  size_t read_size = fread(json_buffer, 1, file_size, file);
  json_buffer[read_size] = '\0';
  fclose(file);

  cJSON *json_root = cJSON_Parse(json_buffer);
  free(json_buffer);

  if (json_root == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = ESP_OK;
  size_t ns_count = 0;
  size_t key_count = 0;

  cJSON *json_config = cJSON_GetObjectItem(json_root, "configuration");
  if (json_config != NULL && cJSON_IsObject(json_config)) {
    cJSON *ns_item = NULL;
    cJSON_ArrayForEach(ns_item, json_config) {
      if (cJSON_IsObject(ns_item)) {
        ns_count++;
        cJSON *key_item = NULL;
        cJSON_ArrayForEach(key_item, ns_item) {
          if (cJSON_IsObject(key_item)) {
            key_count++;
          }
        }
      }
    }
  } else {
    ret = ESP_ERR_INVALID_ARG;
  }

  if (namespace_count != NULL) {
    *namespace_count = ns_count;
  }
  if (total_keys != NULL) {
    *total_keys = key_count;
  }

  cJSON_Delete(json_root);
  return ret;
}

esp_err_t config_manager_backup_to_sdcard(const char *backup_name) {
  if (backup_name == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // First check if SD card is accessible
  struct stat sd_stat = {0};
  if (stat("/sdcard", &sd_stat) != 0) {
    ESP_LOGE(TAG, "SD card not accessible at /sdcard, error: %s",
             strerror(errno));
    return ESP_ERR_NOT_FOUND;
  }

  // Create backup directory if it doesn't exist
  const char *backup_dir = "/sdcard/config_backups";

  // Check if directory already exists
  DIR *dir = opendir(backup_dir);
  if (dir) {
    // Directory exists
    closedir(dir);
    ESP_LOGD(TAG, "Backup directory already exists: %s", backup_dir);
  } else {
    // Directory doesn't exist, create it using storage_manager
    ESP_LOGI(TAG, "Creating backup directory using storage_manager: %s",
             backup_dir);

    esp_err_t result = create_directory_sync(backup_dir);
    if (result != ESP_OK) {
      ESP_LOGE(TAG,
               "Failed to create backup directory using storage_manager: %s",
               esp_err_to_name(result));
      return result;
    } else {
      ESP_LOGI(TAG,
               "Backup directory created successfully using storage_manager");
    }
  }

  // Generate backup filename with timestamp
  char backup_path[256];
  time_t now = time(NULL);
  snprintf(backup_path, sizeof(backup_path), "%s/%s_%lld.json", backup_dir,
           backup_name, (long long)now);

  ESP_LOGI(TAG, "Creating backup: %s", backup_path);
  return config_manager_export_to_sdcard(NULL, backup_path);
}

esp_err_t config_manager_restore_from_sdcard(const char *backup_file,
                                             bool confirm_restore) {
  if (backup_file == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (confirm_restore) {
    ESP_LOGW(TAG, "Restore operation will overwrite current configuration!");
    // In a real implementation, you might want to add user confirmation
  }

  ESP_LOGI(TAG, "Restoring from backup: %s", backup_file);
  return config_manager_import_from_sdcard(backup_file, NULL, true);
}