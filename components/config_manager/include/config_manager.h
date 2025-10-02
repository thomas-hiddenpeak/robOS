/**
 * @file config_manager.h
 * @brief Configuration Manager Component for robOS
 *
 * This component provides unified configuration management using NVS storage.
 * It supports saving and loading configurations for all system components.
 *
 * Features:
 * - Unified NVS interface for all components
 * - Type-safe configuration operations
 * - Automatic backup and restore
 * - Configuration validation
 * - Bulk operations for efficiency
 *
 * @version 1.0.0
 * @date 2025-09-28
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "esp_err.h"
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

#define CONFIG_MANAGER_MAX_KEY_LENGTH 32 ///< Maximum configuration key length
#define CONFIG_MANAGER_MAX_NAMESPACE_LENGTH 16 ///< Maximum namespace length
#define CONFIG_MANAGER_MAX_STRING_LENGTH 256   ///< Maximum string value length

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * @brief Configuration data types
 */
typedef enum {
  CONFIG_TYPE_UINT8,  ///< 8-bit unsigned integer
  CONFIG_TYPE_UINT16, ///< 16-bit unsigned integer
  CONFIG_TYPE_UINT32, ///< 32-bit unsigned integer
  CONFIG_TYPE_INT8,   ///< 8-bit signed integer
  CONFIG_TYPE_INT16,  ///< 16-bit signed integer
  CONFIG_TYPE_INT32,  ///< 32-bit signed integer
  CONFIG_TYPE_FLOAT,  ///< 32-bit floating point
  CONFIG_TYPE_BOOL,   ///< Boolean value
  CONFIG_TYPE_STRING, ///< Null-terminated string
  CONFIG_TYPE_BLOB,   ///< Binary data blob
  CONFIG_TYPE_INVALID ///< Invalid/unknown type
} config_type_t;

// Aliases for command line interface compatibility
#define CONFIG_TYPE_U8 CONFIG_TYPE_UINT8
#define CONFIG_TYPE_U16 CONFIG_TYPE_UINT16
#define CONFIG_TYPE_U32 CONFIG_TYPE_UINT32
#define CONFIG_TYPE_I8 CONFIG_TYPE_INT8
#define CONFIG_TYPE_I16 CONFIG_TYPE_INT16
#define CONFIG_TYPE_I32 CONFIG_TYPE_INT32

/**
 * @brief Configuration value union
 */
typedef union {
  uint8_t u8;                                 ///< 8-bit unsigned integer value
  uint16_t u16;                               ///< 16-bit unsigned integer value
  uint32_t u32;                               ///< 32-bit unsigned integer value
  int8_t i8;                                  ///< 8-bit signed integer value
  int16_t i16;                                ///< 16-bit signed integer value
  int32_t i32;                                ///< 32-bit signed integer value
  float f;                                    ///< Float value
  bool b;                                     ///< Boolean value
  char str[CONFIG_MANAGER_MAX_STRING_LENGTH]; ///< String value
  struct {
    void *data;  ///< Pointer to blob data
    size_t size; ///< Size of blob data
  } blob;        ///< Blob value
} config_value_t;

/**
 * @brief Configuration item structure
 */
typedef struct {
  const char *key;      ///< Configuration key
  config_type_t type;   ///< Data type
  config_value_t value; ///< Configuration value
  bool is_default;      ///< True if this is a default value
} config_item_t;

/**
 * @brief Configuration manager initialization structure
 */
typedef struct {
  bool auto_commit;            ///< Automatically commit changes
  bool create_backup;          ///< Create backup before write operations
  uint32_t commit_interval_ms; ///< Auto-commit interval (0 = disabled)
} config_manager_config_t;

/* ============================================================================
 * Public Function Declarations
 * ============================================================================
 */

/**
 * @brief Get default configuration for config manager
 * @return Default configuration structure
 */
config_manager_config_t config_manager_get_default_config(void);

/**
 * @brief Initialize the configuration manager
 * @param config Configuration parameters (NULL for defaults)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_init(const config_manager_config_t *config);

/**
 * @brief Deinitialize the configuration manager
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_deinit(void);

/**
 * @brief Check if configuration manager is initialized
 * @return true if initialized, false otherwise
 */
bool config_manager_is_initialized(void);

/**
 * @brief Set a configuration value
 * @param namespace Configuration namespace
 * @param key Configuration key
 * @param type Data type
 * @param value Pointer to value data
 * @param size Size of value data (for blob type)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set(const char *namespace, const char *key,
                             config_type_t type, const void *value,
                             size_t size);

/**
 * @brief Get a configuration value
 * @param namespace Configuration namespace
 * @param key Configuration key
 * @param type Expected data type
 * @param value Pointer to store retrieved value
 * @param size Pointer to size variable (for blob type)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_get(const char *namespace, const char *key,
                             config_type_t type, void *value, size_t *size);

/**
 * @brief Delete a configuration key
 * @param namespace Configuration namespace
 * @param key Configuration key
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_delete(const char *namespace, const char *key);

/**
 * @brief Clear all configurations in a namespace
 * @param namespace Configuration namespace
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_clear_namespace(const char *namespace);

/**
 * @brief Check if a configuration key exists
 * @param namespace Configuration namespace
 * @param key Configuration key
 * @return true if exists, false otherwise
 */
bool config_manager_exists(const char *namespace, const char *key);

/**
 * @brief Commit all pending changes to NVS
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_commit(void);

/**
 * @brief Save multiple configuration items at once
 * @param namespace Configuration namespace
 * @param items Array of configuration items
 * @param count Number of items in array
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_save_bulk(const char *namespace,
                                   const config_item_t *items, size_t count);

/**
 * @brief Load multiple configuration items at once
 * @param namespace Configuration namespace
 * @param items Array of configuration items (keys and types must be set)
 * @param count Number of items in array
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_load_bulk(const char *namespace, config_item_t *items,
                                   size_t count);

/**
 * @brief Get statistics about NVS usage
 * @param namespace Configuration namespace (NULL for all)
 * @param used_entries Pointer to store used entries count
 * @param free_entries Pointer to store free entries count
 * @param total_size Pointer to store total size
 * @param used_size Pointer to store used size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_get_stats(const char *namespace, size_t *used_entries,
                                   size_t *free_entries, size_t *total_size,
                                   size_t *used_size);

/* ============================================================================
 * SD Card Import/Export Functions
 * ============================================================================
 */

/**
 * @brief Export configuration namespace to SD card JSON file
 * @param namespace Configuration namespace to export (NULL for all namespaces)
 * @param file_path Full path to destination file on SD card
 * @return ESP_OK on success, error code otherwise
 *
 * @note The exported JSON format includes metadata like type information
 *       and timestamps for proper import validation.
 */
esp_err_t config_manager_export_to_sdcard(const char *namespace,
                                          const char *file_path);

/**
 * @brief Import configuration from SD card JSON file
 * @param file_path Full path to source file on SD card
 * @param namespace Target namespace (NULL to import to original namespaces)
 * @param overwrite Whether to overwrite existing keys
 * @return ESP_OK on success, error code otherwise
 *
 * @note If namespace is specified, all imported keys will be placed in that
 * namespace. If namespace is NULL, keys will be imported to their original
 * namespaces.
 */
esp_err_t config_manager_import_from_sdcard(const char *file_path,
                                            const char *namespace,
                                            bool overwrite);

/**
 * @brief Validate SD card configuration file format
 * @param file_path Full path to file on SD card
 * @param namespace_count Pointer to store number of namespaces (can be NULL)
 * @param total_keys Pointer to store total number of keys (can be NULL)
 * @return ESP_OK if file is valid, error code otherwise
 */
esp_err_t config_manager_validate_sdcard_file(const char *file_path,
                                              size_t *namespace_count,
                                              size_t *total_keys);

/**
 * @brief Create backup of current configuration to SD card
 * @param backup_name Name for backup file (without extension)
 * @return ESP_OK on success, error code otherwise
 *
 * @note The backup file will be created as
 * /sdcard/config_backups/<backup_name>_<timestamp>.json
 */
esp_err_t config_manager_backup_to_sdcard(const char *backup_name);

/**
 * @brief Restore configuration from SD card backup
 * @param backup_file Full path to backup file
 * @param confirm_restore Whether to require confirmation for destructive
 * operations
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_restore_from_sdcard(const char *backup_file,
                                             bool confirm_restore);

/* ============================================================================
 * Convenience Macros
 * ============================================================================
 */

#define CONFIG_SET_U8(ns, key, val)                                            \
  config_manager_set(ns, key, CONFIG_TYPE_UINT8, &(val), sizeof(uint8_t))
#define CONFIG_SET_U16(ns, key, val)                                           \
  config_manager_set(ns, key, CONFIG_TYPE_UINT16, &(val), sizeof(uint16_t))
#define CONFIG_SET_U32(ns, key, val)                                           \
  config_manager_set(ns, key, CONFIG_TYPE_UINT32, &(val), sizeof(uint32_t))
#define CONFIG_SET_I8(ns, key, val)                                            \
  config_manager_set(ns, key, CONFIG_TYPE_INT8, &(val), sizeof(int8_t))
#define CONFIG_SET_I16(ns, key, val)                                           \
  config_manager_set(ns, key, CONFIG_TYPE_INT16, &(val), sizeof(int16_t))
#define CONFIG_SET_I32(ns, key, val)                                           \
  config_manager_set(ns, key, CONFIG_TYPE_INT32, &(val), sizeof(int32_t))
#define CONFIG_SET_FLOAT(ns, key, val)                                         \
  config_manager_set(ns, key, CONFIG_TYPE_FLOAT, &(val), sizeof(float))
#define CONFIG_SET_BOOL(ns, key, val)                                          \
  config_manager_set(ns, key, CONFIG_TYPE_BOOL, &(val), sizeof(bool))
#define CONFIG_SET_STR(ns, key, val)                                           \
  config_manager_set(ns, key, CONFIG_TYPE_STRING, val, strlen(val) + 1)
#define CONFIG_SET_BLOB(ns, key, val, sz)                                      \
  config_manager_set(ns, key, CONFIG_TYPE_BLOB, val, sz)

#define CONFIG_GET_U8(ns, key, val)                                            \
  config_manager_get(ns, key, CONFIG_TYPE_UINT8, val, NULL)
#define CONFIG_GET_U16(ns, key, val)                                           \
  config_manager_get(ns, key, CONFIG_TYPE_UINT16, val, NULL)
#define CONFIG_GET_U32(ns, key, val)                                           \
  config_manager_get(ns, key, CONFIG_TYPE_UINT32, val, NULL)
#define CONFIG_GET_I8(ns, key, val)                                            \
  config_manager_get(ns, key, CONFIG_TYPE_INT8, val, NULL)
#define CONFIG_GET_I16(ns, key, val)                                           \
  config_manager_get(ns, key, CONFIG_TYPE_INT16, val, NULL)
#define CONFIG_GET_I32(ns, key, val)                                           \
  config_manager_get(ns, key, CONFIG_TYPE_INT32, val, NULL)
#define CONFIG_GET_FLOAT(ns, key, val)                                         \
  config_manager_get(ns, key, CONFIG_TYPE_FLOAT, val, NULL)
#define CONFIG_GET_BOOL(ns, key, val)                                          \
  config_manager_get(ns, key, CONFIG_TYPE_BOOL, val, NULL)
#define CONFIG_GET_STR(ns, key, val, sz)                                       \
  config_manager_get(ns, key, CONFIG_TYPE_STRING, val, sz)
#define CONFIG_GET_BLOB(ns, key, val, sz)                                      \
  config_manager_get(ns, key, CONFIG_TYPE_BLOB, val, sz)

/* ============================================================================
 * Command Line Interface Functions
 * ============================================================================
 */

/**
 * @brief Register configuration management console commands
 *
 * This function registers the 'config' command and all its subcommands
 * with the console system. Must be called after console_core_init().
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_register_commands(void);

/**
 * @brief Initialize configuration command testing
 *
 * This function is used for testing the config command system.
 * It creates a task that registers the config commands.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t test_config_commands_init(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H