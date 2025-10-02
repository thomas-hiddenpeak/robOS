# Config Manager SD Card Import/Export Feature

## Overview

The Config Manager component now supports importing and exporting configuration data to/from SD card JSON files. This feature enables:

- Backup and restore of system configurations
- Configuration sharing between devices  
- Batch configuration management
- Configuration version control

## New API Functions

### Export Functions

#### `config_manager_export_to_sdcard()`
```c
esp_err_t config_manager_export_to_sdcard(const char *namespace, const char *file_path);
```
Exports a configuration namespace to a JSON file on SD card.

**Parameters:**
- `namespace`: Configuration namespace to export (NULL for all namespaces)
- `file_path`: Full path to destination file on SD card

**Returns:** ESP_OK on success, error code otherwise

#### `config_manager_backup_to_sdcard()`
```c
esp_err_t config_manager_backup_to_sdcard(const char *backup_name);
```
Creates a timestamped backup of all configurations to `/sdcard/config_backups/`.

**Parameters:**
- `backup_name`: Name for backup file (without extension)

**Returns:** ESP_OK on success, error code otherwise

### Import Functions

#### `config_manager_import_from_sdcard()`
```c
esp_err_t config_manager_import_from_sdcard(const char *file_path, const char *namespace, bool overwrite);
```
Imports configuration from SD card JSON file.

**Parameters:**
- `file_path`: Full path to source file on SD card
- `namespace`: Target namespace (NULL to import to original namespaces)
- `overwrite`: Whether to overwrite existing keys

**Returns:** ESP_OK on success, error code otherwise

#### `config_manager_restore_from_sdcard()`
```c
esp_err_t config_manager_restore_from_sdcard(const char *backup_file, bool confirm_restore);
```
Restores configuration from SD card backup file.

**Parameters:**
- `backup_file`: Full path to backup file
- `confirm_restore`: Whether to require confirmation for destructive operations

**Returns:** ESP_OK on success, error code otherwise

### Utility Functions

#### `config_manager_validate_sdcard_file()`
```c
esp_err_t config_manager_validate_sdcard_file(const char *file_path, size_t *namespace_count, size_t *total_keys);
```
Validates SD card configuration file format.

**Parameters:**
- `file_path`: Full path to file on SD card
- `namespace_count`: Pointer to store number of namespaces (can be NULL)
- `total_keys`: Pointer to store total number of keys (can be NULL)

**Returns:** ESP_OK if file is valid, error code otherwise

## Console Commands

### Export Commands

```bash
# Export specific namespace to SD card
config backup export led_matrix /sdcard/led_config.json

# Create timestamped backup
config backup create system_backup
```

### Import Commands

```bash
# Import from SD card to original namespaces
config backup import /sdcard/led_config.json

# Import to specific namespace with overwrite
config backup import /sdcard/config.json new_namespace true

# Restore from backup (requires confirmation)
config backup restore /sdcard/config_backups/backup.json confirm
```

### Utility Commands

```bash
# Validate configuration file
config backup validate /sdcard/config.json
```

## JSON File Format

The exported JSON files use the following format:

```json
{
  "format_version": "1.0",
  "export_time": "2025-10-03T10:30:00Z",
  "device_id": "robOS",
  "configuration": {
    "namespace1": {
      "key1": {
        "type": "uint32",
        "value": 42
      },
      "key2": {
        "type": "string", 
        "value": "example"
      },
      "key3": {
        "type": "bool",
        "value": true
      }
    },
    "namespace2": {
      "setting1": {
        "type": "float",
        "value": 3.14
      }
    }
  }
}
```

### Supported Data Types

- `uint8`, `uint16`, `uint32` - Unsigned integers
- `int8`, `int16`, `int32` - Signed integers  
- `float` - Floating point numbers
- `bool` - Boolean values
- `string` - Text strings
- `blob` - Binary data (not fully implemented)

## Usage Examples

### Configuration Migration

```c
// Export LED configuration
esp_err_t ret = config_manager_export_to_sdcard("led_matrix", "/sdcard/led_backup.json");

// Later, import on another device
ret = config_manager_import_from_sdcard("/sdcard/led_backup.json", NULL, true);
```

### System Backup

```c
// Create full system backup
esp_err_t ret = config_manager_backup_to_sdcard("daily_backup");

// Restore if needed
ret = config_manager_restore_from_sdcard("/sdcard/config_backups/daily_backup_1696334400.json", true);
```

### Configuration Validation

```c
size_t namespace_count, total_keys;
esp_err_t ret = config_manager_validate_sdcard_file("/sdcard/config.json", &namespace_count, &total_keys);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Valid config file: %zu namespaces, %zu keys", namespace_count, total_keys);
}
```

## Error Handling

The functions return standard ESP-IDF error codes:

- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_ERR_INVALID_STATE` - Config manager not initialized
- `ESP_ERR_NOT_FOUND` - File not found
- `ESP_ERR_NO_MEM` - Out of memory
- `ESP_ERR_INVALID_SIZE` - File size invalid
- `ESP_ERR_TIMEOUT` - Mutex timeout
- `ESP_ERR_NOT_SUPPORTED` - Feature not implemented

## Dependencies

The SD card import/export feature requires:

- cJSON library for JSON parsing
- FATFS/SD card support in ESP-IDF
- Sufficient heap memory for JSON processing

## Limitations

1. Maximum file size: 64KB
2. Blob data export not fully implemented
3. Timestamp generation needs proper RTC setup
4. All namespaces export not fully implemented (only specific namespace supported)

## Future Enhancements

- Base64 encoding for blob data
- Compression for large configuration files
- Incremental/differential backups
- Configuration file encryption
- Multiple backup retention policies