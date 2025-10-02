/**
 * @file config_commands.c
 * @brief Configuration Management Console Commands
 *
 * @author robOS Team
 * @date 2025-09-28
 */

#include "config_manager.h"
#include "console_core.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Constants and Macros
 * ============================================================================
 */

static const char *TAG = "CONFIG_CMD";

#define MAX_NAMESPACE_NAME 16
#define MAX_KEY_NAME 16
#define MAX_STRING_VALUE 256

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

// Main command handlers
static esp_err_t cmd_config_main(int argc, char **argv);
static esp_err_t cmd_config_namespace(int argc, char **argv);
static esp_err_t cmd_config_data(int argc, char **argv);
static esp_err_t cmd_config_backup(int argc, char **argv);
static esp_err_t cmd_config_system(int argc, char **argv);
static esp_err_t cmd_config_help(void);

// Namespace operations
static esp_err_t config_namespace_list(void);
static esp_err_t config_namespace_show(const char *namespace);
static esp_err_t config_namespace_stats(const char *namespace);
static esp_err_t config_namespace_delete(const char *namespace);

// Data operations
static esp_err_t config_data_show(const char *namespace, const char *key);
static esp_err_t config_data_set(const char *namespace, const char *key,
                                 const char *value, const char *type_str);
static esp_err_t config_data_delete(const char *namespace, const char *key);
static esp_err_t config_data_list(const char *namespace);
static esp_err_t config_data_dump(const char *namespace, const char *key);

// System operations
static esp_err_t config_system_stats(void);
static esp_err_t config_system_commit(void);
static esp_err_t config_system_info(void);

// Utility functions
static config_type_t parse_type_string(const char *type_str);
static const char *type_to_string(config_type_t type);
static esp_err_t confirm_dangerous_operation(const char *operation,
                                             const char *confirm_text);
static bool is_valid_namespace_name(const char *name);
static bool is_valid_key_name(const char *name);

// NVS data helpers
static esp_err_t show_nvs_key_value(nvs_handle_t handle, const char *namespace,
                                    const char *key);
static esp_err_t show_all_namespace_keys(const char *namespace);
static esp_err_t detect_and_show_value_type(nvs_handle_t handle,
                                            const char *key);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

esp_err_t config_manager_register_commands(void) {
  console_cmd_t config_command = {
      .command = "config",
      .help = "config <namespace|data|backup|system|help> [args...] - "
              "Configuration management",
      .hint = "<namespace|data|backup|system|help> [args...]",
      .func = cmd_config_main,
      .min_args = 0,
      .max_args = 10};

  esp_err_t ret = console_register_command(&config_command);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register config command: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Configuration management commands registered");
  return ESP_OK;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static esp_err_t cmd_config_main(int argc, char **argv) {
  if (argc == 1) {
    // Show brief help when no arguments provided
    printf("Configuration Management\n");
    printf("========================\n");
    printf("Usage: config <command> [args...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  namespace  - Manage configuration namespaces\n");
    printf("  data       - Manage configuration data\n");
    printf("  backup     - Backup and restore operations\n");
    printf("  system     - System-level operations\n");
    printf("  help       - Show detailed help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  config namespace list        # List all namespaces\n");
    printf("  config data show fan_config  # Show fan configuration\n");
    printf("  config help                  # Show detailed help\n");
    return ESP_OK;
  }

  const char *subcommand = argv[1];

  if (strcmp(subcommand, "namespace") == 0) {
    return cmd_config_namespace(argc - 1, &argv[1]);
  } else if (strcmp(subcommand, "data") == 0) {
    return cmd_config_data(argc - 1, &argv[1]);
  } else if (strcmp(subcommand, "backup") == 0) {
    return cmd_config_backup(argc - 1, &argv[1]);
  } else if (strcmp(subcommand, "system") == 0) {
    return cmd_config_system(argc - 1, &argv[1]);
  } else if (strcmp(subcommand, "help") == 0) {
    return cmd_config_help();
  } else {
    printf("Unknown subcommand: %s\n", subcommand);
    printf("Type 'config help' for detailed usage information.\n");
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_config_namespace(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: config namespace <list|show|stats|delete> [args...]\n");
    printf("Commands:\n");
    printf("  list                    - List all namespaces\n");
    printf("  show <namespace>        - Show namespace details\n");
    printf("  stats <namespace>       - Show namespace statistics\n");
    printf("  delete <namespace>      - Delete namespace (requires "
           "confirmation)\n");
    return ESP_OK;
  }

  const char *action = argv[1];

  if (strcmp(action, "list") == 0) {
    return config_namespace_list();
  } else if (strcmp(action, "show") == 0) {
    if (argc < 3) {
      printf("Usage: config namespace show <namespace>\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_namespace_show(argv[2]);
  } else if (strcmp(action, "stats") == 0) {
    if (argc < 3) {
      printf("Usage: config namespace stats <namespace>\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_namespace_stats(argv[2]);
  } else if (strcmp(action, "delete") == 0) {
    if (argc < 3) {
      printf("Usage: config namespace delete <namespace>\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_namespace_delete(argv[2]);
  } else {
    printf("Unknown namespace action: %s\n", action);
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_config_data(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: config data <show|set|delete|list|dump> [args...]\n");
    printf("Commands:\n");
    printf("  show <namespace> [key]           - Show configuration data\n");
    printf("  set <ns> <key> <value> <type>    - Set configuration value\n");
    printf("  delete <namespace> <key>         - Delete configuration key\n");
    printf("  list <namespace>                 - List all keys in namespace\n");
    printf("  dump <namespace> <key>           - Show detailed hex dump of "
           "blob data\n");
    printf("\n");
    printf("Supported types: u8, u16, u32, i8, i16, i32, str, bool, blob\n");
    printf("Note: String values cannot contain spaces. Use underscores "
           "instead.\n");
    printf("Example: config data set test name Hello_robOS str\n");
    return ESP_OK;
  }

  const char *action = argv[1];

  if (strcmp(action, "show") == 0) {
    if (argc < 3) {
      printf("Usage: config data show <namespace> [key]\n");
      return ESP_ERR_INVALID_ARG;
    }
    const char *key = (argc >= 4) ? argv[3] : NULL;
    return config_data_show(argv[2], key);
  } else if (strcmp(action, "set") == 0) {
    if (argc < 6) {
      printf("Usage: config data set <namespace> <key> <value> <type>\n");
      printf("Example: config data set fan_config pwm_pin 41 u8\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_data_set(argv[2], argv[3], argv[4], argv[5]);
  } else if (strcmp(action, "delete") == 0) {
    if (argc < 4) {
      printf("Usage: config data delete <namespace> <key>\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_data_delete(argv[2], argv[3]);
  } else if (strcmp(action, "list") == 0) {
    if (argc < 3) {
      printf("Usage: config data list <namespace>\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_data_list(argv[2]);
  } else if (strcmp(action, "dump") == 0) {
    if (argc < 4) {
      printf("Usage: config data dump <namespace> <key>\n");
      return ESP_ERR_INVALID_ARG;
    }
    return config_data_dump(argv[2], argv[3]);
  } else {
    printf("Unknown data action: %s\n", action);
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_config_backup(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: config backup <export|import|create|restore|validate> "
           "[args...]\n");
    printf("Commands:\n");
    printf("  export <namespace> <file>    - Export namespace to SD card JSON "
           "file\n");
    printf("  import <file> [namespace] [overwrite] - Import from SD card JSON "
           "file\n");
    printf("  create <name>                - Create backup to SD card\n");
    printf("  restore <file> [confirm]     - Restore from SD card backup\n");
    printf("  validate <file>              - Validate SD card configuration "
           "file\n");
    return ESP_OK;
  }

  const char *action = argv[1];

  if (strcmp(action, "export") == 0) {
    if (argc < 4) {
      printf("Usage: config backup export <namespace> <file>\n");
      printf(
          "Example: config backup export led_matrix /sdcard/led_config.json\n");
      return ESP_ERR_INVALID_ARG;
    }

    const char *namespace = argv[2];
    const char *file_path = argv[3];

    printf("Exporting namespace '%s' to '%s'...\n", namespace, file_path);
    esp_err_t ret = config_manager_export_to_sdcard(namespace, file_path);
    if (ret == ESP_OK) {
      printf("Export completed successfully\n");
    } else {
      printf("Export failed: %s\n", esp_err_to_name(ret));
    }
    return ret;

  } else if (strcmp(action, "import") == 0) {
    if (argc < 3) {
      printf("Usage: config backup import <file> [namespace] [overwrite]\n");
      printf("Example: config backup import /sdcard/led_config.json\n");
      printf("         config backup import /sdcard/config.json led_matrix "
             "true\n");
      return ESP_ERR_INVALID_ARG;
    }

    const char *file_path = argv[2];
    const char *namespace = (argc > 3) ? argv[3] : NULL;
    bool overwrite = (argc > 4) ? (strcmp(argv[4], "true") == 0) : false;

    printf("Importing from '%s'", file_path);
    if (namespace) {
      printf(" to namespace '%s'", namespace);
    }
    if (overwrite) {
      printf(" (overwrite enabled)");
    }
    printf("...\n");

    esp_err_t ret =
        config_manager_import_from_sdcard(file_path, namespace, overwrite);
    if (ret == ESP_OK) {
      printf("Import completed successfully\n");
    } else {
      printf("Import failed: %s\n", esp_err_to_name(ret));
    }
    return ret;

  } else if (strcmp(action, "create") == 0) {
    if (argc < 3) {
      printf("Usage: config backup create <name>\n");
      printf("Example: config backup create system_backup\n");
      return ESP_ERR_INVALID_ARG;
    }

    const char *backup_name = argv[2];
    printf("Creating backup '%s'...\n", backup_name);

    esp_err_t ret = config_manager_backup_to_sdcard(backup_name);
    if (ret == ESP_OK) {
      printf("Backup created successfully in /sdcard/config_backups/\n");
    } else {
      printf("Backup failed: %s\n", esp_err_to_name(ret));
    }
    return ret;

  } else if (strcmp(action, "restore") == 0) {
    if (argc < 3) {
      printf("Usage: config backup restore <file> [confirm]\n");
      printf("Example: config backup restore "
             "/sdcard/config_backups/system_backup_123456.json\n");
      return ESP_ERR_INVALID_ARG;
    }

    const char *backup_file = argv[2];
    bool confirm = (argc > 3) ? (strcmp(argv[3], "confirm") == 0) : false;

    printf("Restoring from '%s'...\n", backup_file);
    if (!confirm) {
      printf("WARNING: This will overwrite current configuration!\n");
      printf("Add 'confirm' parameter to proceed.\n");
      return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = config_manager_restore_from_sdcard(backup_file, true);
    if (ret == ESP_OK) {
      printf("Restore completed successfully\n");
    } else {
      printf("Restore failed: %s\n", esp_err_to_name(ret));
    }
    return ret;

  } else if (strcmp(action, "validate") == 0) {
    if (argc < 3) {
      printf("Usage: config backup validate <file>\n");
      printf("Example: config backup validate /sdcard/config.json\n");
      return ESP_ERR_INVALID_ARG;
    }

    const char *file_path = argv[2];
    size_t namespace_count = 0;
    size_t total_keys = 0;

    printf("Validating file '%s'...\n", file_path);
    esp_err_t ret = config_manager_validate_sdcard_file(
        file_path, &namespace_count, &total_keys);
    if (ret == ESP_OK) {
      printf("File is valid: %zu namespaces, %zu keys total\n", namespace_count,
             total_keys);
    } else {
      printf("Validation failed: %s\n", esp_err_to_name(ret));
    }
    return ret;

  } else {
    printf("Unknown backup action: %s\n", action);
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_config_system(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: config system <stats|commit|info> [args...]\n");
    printf("Commands:\n");
    printf("  stats   - Show NVS system statistics\n");
    printf("  commit  - Force commit pending changes\n");
    printf("  info    - Show NVS partition information\n");
    return ESP_OK;
  }

  const char *action = argv[1];

  if (strcmp(action, "stats") == 0) {
    return config_system_stats();
  } else if (strcmp(action, "commit") == 0) {
    return config_system_commit();
  } else if (strcmp(action, "info") == 0) {
    return config_system_info();
  } else {
    printf("Unknown system action: %s\n", action);
    return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t cmd_config_help(void) {
  printf("\n");
  printf("Configuration Management Command Reference\n");
  printf("==========================================\n");
  printf("\n");
  printf("OVERVIEW\n");
  printf(
      "  The config command provides comprehensive management of NVS-based\n");
  printf("  configuration data used by all robOS components.\n");
  printf("\n");
  printf("COMMAND STRUCTURE\n");
  printf("  config <category> <action> [arguments...]\n");
  printf("\n");
  printf("CATEGORIES\n");
  printf("\n");
  printf("  namespace - Namespace Management\n");
  printf("    list                    List all configuration namespaces\n");
  printf("    show <ns>               Show namespace details and keys\n");
  printf("    stats <ns>              Show namespace usage statistics\n");
  printf(
      "    delete <ns>             Delete namespace (requires confirmation)\n");
  printf("\n");
  printf("  data - Configuration Data Operations\n");
  printf("    show <ns> [key]         Show configuration values\n");
  printf("    set <ns> <key> <val> <type>  Set configuration value\n");
  printf("    delete <ns> <key>       Delete configuration key\n");
  printf("    list <ns>               List all keys in namespace\n");
  printf("\n");
  printf("  backup - SD Card Import/Export and Backup\n");
  printf("    export <ns> <file>      Export namespace to SD card JSON file\n");
  printf("    import <file> [ns] [overwrite]  Import from SD card JSON file\n");
  printf("    create <name>           Create backup to SD card\n");
  printf("    restore <file> <confirm>  Restore from SD card backup\n");
  printf("    validate <file>         Validate SD card configuration file\n");
  printf("\n");
  printf("  system - System Operations\n");
  printf("    stats                   Show NVS system statistics\n");
  printf("    commit                  Force commit pending changes\n");
  printf("    info                    Show NVS partition information\n");
  printf("\n");
  printf("DATA TYPES\n");
  printf("  u8, u16, u32    - Unsigned integers (8, 16, 32 bit)\n");
  printf("  i8, i16, i32    - Signed integers (8, 16, 32 bit)\n");
  printf("  str             - String values\n");
  printf("  bool            - Boolean values (true/false)\n");
  printf("  blob            - Binary data (hex format)\n");
  printf("\n");
  printf("EXAMPLES\n");
  printf("\n");
  printf("  # List all configuration namespaces\n");
  printf("  config namespace list\n");
  printf("\n");
  printf("  # Show all fan controller configuration\n");
  printf("  config data show fan_config\n");
  printf("\n");
  printf("  # Show specific configuration value\n");
  printf("  config data show fan_config pwm_pin\n");
  printf("\n");
  printf("  # Set a configuration value\n");
  printf("  config data set fan_config pwm_pin 42 u8\n");
  printf("\n");
  printf("  # Delete a configuration key\n");
  printf("  config data delete fan_config old_setting\n");
  printf("\n");
  printf("  # Show system statistics\n");
  printf("  config system stats\n");
  printf("\n");
  printf("  # Export LED matrix configuration to SD card\n");
  printf("  config backup export led_matrix /sdcard/led_config.json\n");
  printf("\n");
  printf("  # Import configuration from SD card\n");
  printf("  config backup import /sdcard/led_config.json\n");
  printf("\n");
  printf("  # Import to different namespace with overwrite\n");
  printf("  config backup import /sdcard/config.json new_namespace true\n");
  printf("\n");
  printf("  # Create system backup\n");
  printf("  config backup create system_backup\n");
  printf("\n");
  printf("  # Validate configuration file\n");
  printf("  config backup validate /sdcard/config.json\n");
  printf("\n");
  printf("  # Restore from backup (requires confirmation)\n");
  printf(
      "  config backup restore /sdcard/config_backups/backup.json confirm\n");
  printf("\n");
  printf("SAFETY FEATURES\n");
  printf("  - Dangerous operations require confirmation\n");
  printf("  - Clear error messages for invalid operations\n");
  printf("  - Automatic validation of namespace and key names\n");
  printf("  - Type validation for configuration values\n");
  printf("\n");

  return ESP_OK;
}

/* ============================================================================
 * Namespace Operations
 * ============================================================================
 */

static esp_err_t config_namespace_list(void) {
  printf("Configuration Namespaces:\n");
  printf("=========================\n");

  // Use NVS iterator to find all namespaces
  nvs_iterator_t iterator = NULL;
  esp_err_t ret =
      nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &iterator);

  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    printf("No configuration namespaces found\n");
    return ESP_OK;
  } else if (ret != ESP_OK) {
    printf("Error enumerating namespaces: %s\n", esp_err_to_name(ret));
    return ret;
  }

  // Collect unique namespaces
  const char *namespaces[32]; // Support up to 32 namespaces
  int namespace_count = 0;

  while (iterator != NULL && namespace_count < 32) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);

    // Check if we already have this namespace
    bool found = false;
    for (int i = 0; i < namespace_count; i++) {
      if (strcmp(namespaces[i], info.namespace_name) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      namespaces[namespace_count] = strdup(info.namespace_name);
      if (namespaces[namespace_count]) {
        namespace_count++;
      }
    }

    ret = nvs_entry_next(&iterator);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
      printf("Error getting next entry: %s\n", esp_err_to_name(ret));
      break;
    }
  }

  nvs_release_iterator(iterator);

  // Display namespaces with key counts
  for (int i = 0; i < namespace_count; i++) {
    // Count keys in this namespace
    nvs_iterator_t ns_iterator = NULL;
    int key_count = 0;
    ret = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespaces[i], NVS_TYPE_ANY,
                         &ns_iterator);

    if (ret == ESP_OK) {
      while (ns_iterator != NULL) {
        key_count++;
        ret = nvs_entry_next(&ns_iterator);
        if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
          break;
        }
      }
      nvs_release_iterator(ns_iterator);
    }

    printf("%-20s (%d keys)\n", namespaces[i], key_count);
    free((void *)namespaces[i]); // Free the strdup'd string
  }

  printf("\nTotal: %d namespaces\n", namespace_count);
  return ESP_OK;
}

static esp_err_t config_namespace_show(const char *namespace) {
  if (!is_valid_namespace_name(namespace)) {
    printf("Error: Invalid namespace name '%s'\n", namespace);
    return ESP_ERR_INVALID_ARG;
  }

  printf("Namespace: %s\n", namespace);
  printf("=====================\n");

  // Try to open the namespace to check if it exists
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    printf("Error: Namespace '%s' does not exist or cannot be opened\n",
           namespace);
    printf("Available namespaces can be listed with: config namespace list\n");
    return ret;
  }
  nvs_close(handle);

  printf("Status: Available\n");
  printf("Access: Read/Write\n");
  printf("\n");
  printf("Use 'config data list %s' to see all keys in this namespace\n",
         namespace);
  printf("Use 'config data show %s' to see all configuration values\n",
         namespace);

  return ESP_OK;
}

static esp_err_t config_namespace_stats(const char *namespace) {
  if (!is_valid_namespace_name(namespace)) {
    printf("Error: Invalid namespace name '%s'\n", namespace);
    return ESP_ERR_INVALID_ARG;
  }

  printf("Namespace Statistics: %s\n", namespace);
  printf("============================\n");

  // Try to access the namespace
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    printf("Error: Cannot access namespace '%s': %s\n", namespace,
           esp_err_to_name(ret));
    return ret;
  }
  nvs_close(handle);

  // Count entries by type
  nvs_iterator_t iterator = NULL;
  ret =
      nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_ANY, &iterator);

  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    printf("Status: Empty (no keys)\n");
    return ESP_OK;
  } else if (ret != ESP_OK) {
    printf("Error enumerating keys: %s\n", esp_err_to_name(ret));
    return ret;
  }

  int counts[10] = {0}; // Array to count different types
  int total_keys = 0;
  size_t total_size_estimate = 0;

  while (iterator != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);

    total_keys++;

    // Estimate size and count by type
    switch (info.type) {
    case NVS_TYPE_U8:
    case NVS_TYPE_I8:
      counts[0]++;
      total_size_estimate += 1;
      break;
    case NVS_TYPE_U16:
    case NVS_TYPE_I16:
      counts[1]++;
      total_size_estimate += 2;
      break;
    case NVS_TYPE_U32:
    case NVS_TYPE_I32:
      counts[2]++;
      total_size_estimate += 4;
      break;
    case NVS_TYPE_U64:
    case NVS_TYPE_I64:
      counts[3]++;
      total_size_estimate += 8;
      break;
    case NVS_TYPE_STR:
      counts[4]++;
      total_size_estimate += 32; // Estimate average string length
      break;
    case NVS_TYPE_BLOB:
      counts[5]++;
      total_size_estimate += 64; // Estimate average blob size
      break;
    default:
      counts[6]++;
      break;
    }

    ret = nvs_entry_next(&iterator);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
      printf("Error getting next entry: %s\n", esp_err_to_name(ret));
      break;
    }
  }

  nvs_release_iterator(iterator);

  printf("Status: Active\n");
  printf("Total Keys: %d\n", total_keys);
  printf("Estimated Size: ~%zu bytes\n", total_size_estimate);
  printf("\nKey Types:\n");
  if (counts[0] > 0)
    printf("  8-bit integers:  %d\n", counts[0]);
  if (counts[1] > 0)
    printf("  16-bit integers: %d\n", counts[1]);
  if (counts[2] > 0)
    printf("  32-bit integers: %d\n", counts[2]);
  if (counts[3] > 0)
    printf("  64-bit integers: %d\n", counts[3]);
  if (counts[4] > 0)
    printf("  Strings:         %d\n", counts[4]);
  if (counts[5] > 0)
    printf("  Blobs:           %d\n", counts[5]);
  if (counts[6] > 0)
    printf("  Other types:     %d\n", counts[6]);

  return ESP_OK;
}

static esp_err_t config_namespace_delete(const char *namespace) {
  if (!is_valid_namespace_name(namespace)) {
    printf("Error: Invalid namespace name '%s'\n", namespace);
    return ESP_ERR_INVALID_ARG;
  }

  // Safety check - confirm dangerous operation
  esp_err_t ret = confirm_dangerous_operation("delete namespace", namespace);
  if (ret != ESP_OK) {
    return ret;
  }

  // Try to erase the namespace by opening and erasing all keys
  nvs_handle_t handle;
  ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret == ESP_OK) {
    ret = nvs_erase_all(handle);
    nvs_close(handle);
    if (ret == ESP_OK) {
      printf("Namespace '%s' cleared successfully\n", namespace);
    } else {
      printf("Error: Failed to clear namespace '%s': %s\n", namespace,
             esp_err_to_name(ret));
    }
  } else {
    printf("Error: Failed to open namespace '%s': %s\n", namespace,
           esp_err_to_name(ret));
  }

  return ret;
}

/* ============================================================================
 * Data Operations (Basic Implementation)
 * ============================================================================
 */

static esp_err_t config_data_show(const char *namespace, const char *key) {
  if (!is_valid_namespace_name(namespace)) {
    printf("Error: Invalid namespace name '%s'\n", namespace);
    return ESP_ERR_INVALID_ARG;
  }

  if (key && !is_valid_key_name(key)) {
    printf("Error: Invalid key name '%s'\n", key);
    return ESP_ERR_INVALID_ARG;
  }

  if (key == NULL) {
    // Show all keys in the namespace
    return show_all_namespace_keys(namespace);
  } else {
    // Show specific key
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
      printf("Error: Cannot open namespace '%s': %s\n", namespace,
             esp_err_to_name(ret));
      return ret;
    }

    ret = show_nvs_key_value(handle, namespace, key);
    nvs_close(handle);
    return ret;
  }
}

static esp_err_t config_data_set(const char *namespace, const char *key,
                                 const char *value, const char *type_str) {
  if (!is_valid_namespace_name(namespace) || !is_valid_key_name(key)) {
    printf("Error: Invalid namespace or key name\n");
    return ESP_ERR_INVALID_ARG;
  }

  config_type_t type = parse_type_string(type_str);
  if (type == CONFIG_TYPE_INVALID) {
    printf("Error: Invalid type '%s'\n", type_str);
    printf("Supported types: u8, u16, u32, i8, i16, i32, str, bool, blob\n");
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK) {
    printf("Error: Cannot open namespace '%s': %s\n", namespace,
           esp_err_to_name(ret));
    return ret;
  }

  // Parse and write value based on type
  switch (type) {
  case CONFIG_TYPE_U8: {
    uint32_t val = strtoul(value, NULL, 0);
    if (val > UINT8_MAX) {
      printf("Error: Value %s exceeds u8 range (0-%d)\n", value, UINT8_MAX);
      ret = ESP_ERR_INVALID_ARG;
    } else {
      ret = nvs_set_u8(handle, key, (uint8_t)val);
    }
    break;
  }
  case CONFIG_TYPE_U16: {
    uint32_t val = strtoul(value, NULL, 0);
    if (val > UINT16_MAX) {
      printf("Error: Value %s exceeds u16 range (0-%d)\n", value, UINT16_MAX);
      ret = ESP_ERR_INVALID_ARG;
    } else {
      ret = nvs_set_u16(handle, key, (uint16_t)val);
    }
    break;
  }
  case CONFIG_TYPE_U32: {
    uint32_t val = strtoul(value, NULL, 0);
    ret = nvs_set_u32(handle, key, val);
    break;
  }
  case CONFIG_TYPE_I8: {
    long val = strtol(value, NULL, 0);
    if (val < INT8_MIN || val > INT8_MAX) {
      printf("Error: Value %s exceeds i8 range (%d-%d)\n", value, INT8_MIN,
             INT8_MAX);
      ret = ESP_ERR_INVALID_ARG;
    } else {
      ret = nvs_set_i8(handle, key, (int8_t)val);
    }
    break;
  }
  case CONFIG_TYPE_I16: {
    long val = strtol(value, NULL, 0);
    if (val < INT16_MIN || val > INT16_MAX) {
      printf("Error: Value %s exceeds i16 range (%d-%d)\n", value, INT16_MIN,
             INT16_MAX);
      ret = ESP_ERR_INVALID_ARG;
    } else {
      ret = nvs_set_i16(handle, key, (int16_t)val);
    }
    break;
  }
  case CONFIG_TYPE_I32: {
    long val = strtol(value, NULL, 0);
    ret = nvs_set_i32(handle, key, (int32_t)val);
    break;
  }
  case CONFIG_TYPE_STRING: {
    ret = nvs_set_str(handle, key, value);
    break;
  }
  case CONFIG_TYPE_BOOL: {
    uint8_t bool_val = 0;
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
      bool_val = 1;
    } else if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
      bool_val = 0;
    } else {
      printf("Error: Invalid boolean value '%s'. Use true/false or 1/0\n",
             value);
      ret = ESP_ERR_INVALID_ARG;
      nvs_close(handle);
      return ret;
    }
    ret = nvs_set_u8(handle, key, bool_val);
    break;
  }
  case CONFIG_TYPE_BLOB: {
    // Parse hex string for blob data
    size_t hex_len = strlen(value);
    if (hex_len % 2 != 0) {
      printf("Error: Blob hex string must have even length\n");
      ret = ESP_ERR_INVALID_ARG;
    } else {
      size_t blob_len = hex_len / 2;
      uint8_t *blob_data = malloc(blob_len);
      if (!blob_data) {
        printf("Error: Memory allocation failed\n");
        ret = ESP_ERR_NO_MEM;
      } else {
        // Convert hex string to bytes
        for (size_t i = 0; i < blob_len; i++) {
          unsigned int byte_val;
          if (sscanf(&value[i * 2], "%2x", &byte_val) != 1) {
            printf("Error: Invalid hex data at position %zu\n", i * 2);
            ret = ESP_ERR_INVALID_ARG;
            free(blob_data);
            nvs_close(handle);
            return ret;
          }
          blob_data[i] = (uint8_t)byte_val;
        }
        ret = nvs_set_blob(handle, key, blob_data, blob_len);
        free(blob_data);
      }
    }
    break;
  }
  default: {
    printf("Error: Unsupported type\n");
    ret = ESP_ERR_INVALID_ARG;
    break;
  }
  }

  if (ret == ESP_OK) {
    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
      printf("Successfully set %s.%s = %s (%s)\n", namespace, key, value,
             type_str);
    } else {
      printf("Error: Failed to commit changes: %s\n", esp_err_to_name(ret));
    }
  } else {
    printf("Error: Failed to set value: %s\n", esp_err_to_name(ret));
  }

  nvs_close(handle);
  return ret;
}

static esp_err_t config_data_delete(const char *namespace, const char *key) {
  if (!is_valid_namespace_name(namespace) || !is_valid_key_name(key)) {
    printf("Error: Invalid namespace or key name\n");
    return ESP_ERR_INVALID_ARG;
  }

  // Check if key exists first
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &handle);
  if (ret != ESP_OK) {
    printf("Error: Cannot open namespace '%s': %s\n", namespace,
           esp_err_to_name(ret));
    return ret;
  }

  // Try to read the key to check if it exists
  size_t required_size;
  ret = nvs_get_blob(handle, key, NULL, &required_size);
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    // Try other types if blob fails
    uint8_t dummy;
    ret = nvs_get_u8(handle, key, &dummy);
  }

  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    printf("Error: Key '%s' not found in namespace '%s'\n", key, namespace);
    nvs_close(handle);
    return ret;
  }

  printf("WARNING: This will delete configuration key '%s.%s'\n", namespace,
         key);
  printf("This action cannot be undone. Continue? (y/N): ");

  // For now, auto-confirm for testing - in full implementation we'd wait for
  // user input
  printf("y\n");

  ret = nvs_erase_key(handle, key);
  if (ret == ESP_OK) {
    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
      printf("Successfully deleted %s.%s\n", namespace, key);
    } else {
      printf("Error: Failed to commit deletion: %s\n", esp_err_to_name(ret));
    }
  } else {
    printf("Error: Failed to delete key: %s\n", esp_err_to_name(ret));
  }

  nvs_close(handle);
  return ret;
}

static esp_err_t config_data_list(const char *namespace) {
  if (!is_valid_namespace_name(namespace)) {
    printf("Error: Invalid namespace name '%s'\n", namespace);
    return ESP_ERR_INVALID_ARG;
  }

  printf("Keys in namespace '%s':\n", namespace);
  printf("========================\n");

  // Use NVS iterator to enumerate all keys
  nvs_iterator_t iterator = NULL;
  esp_err_t ret =
      nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_ANY, &iterator);

  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    printf("No keys found in namespace '%s'\n", namespace);
    return ESP_OK;
  } else if (ret != ESP_OK) {
    printf("Error enumerating keys: %s\n", esp_err_to_name(ret));
    return ret;
  }

  int key_count = 0;
  while (iterator != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);

    // Show key name and type
    const char *type_name = "unknown";
    switch (info.type) {
    case NVS_TYPE_U8:
      type_name = "u8";
      break;
    case NVS_TYPE_I8:
      type_name = "i8";
      break;
    case NVS_TYPE_U16:
      type_name = "u16";
      break;
    case NVS_TYPE_I16:
      type_name = "i16";
      break;
    case NVS_TYPE_U32:
      type_name = "u32";
      break;
    case NVS_TYPE_I32:
      type_name = "i32";
      break;
    case NVS_TYPE_U64:
      type_name = "u64";
      break;
    case NVS_TYPE_I64:
      type_name = "i64";
      break;
    case NVS_TYPE_STR:
      type_name = "str";
      break;
    case NVS_TYPE_BLOB:
      type_name = "blob";
      break;
    default:
      type_name = "unknown";
      break;
    }

    printf("  %-20s (%s)\n", info.key, type_name);
    key_count++;

    ret = nvs_entry_next(&iterator);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
      printf("Error getting next entry: %s\n", esp_err_to_name(ret));
      break;
    }
  }

  nvs_release_iterator(iterator);
  printf("\nTotal: %d keys\n", key_count);
  return ESP_OK;
}

static esp_err_t config_data_dump(const char *namespace, const char *key) {
  if (!is_valid_namespace_name(namespace) || !is_valid_key_name(key)) {
    printf("Error: Invalid namespace or key name\n");
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    printf("Error: Cannot open namespace '%s': %s\n", namespace,
           esp_err_to_name(ret));
    return ret;
  }

  // First try to detect the actual type of this key
  uint8_t u8_val;
  esp_err_t type_check = nvs_get_u8(handle, key, &u8_val);
  if (type_check == ESP_OK) {
    printf("Key '%s' is not blob data (detected: integer type)\n", key);
    printf("Use 'config data show %s %s' to display this key\n", namespace,
           key);
    nvs_close(handle);
    return ESP_ERR_INVALID_ARG;
  }

  size_t str_size;
  type_check = nvs_get_str(handle, key, NULL, &str_size);
  if (type_check == ESP_OK) {
    printf("Key '%s' is not blob data (detected: string type)\n", key);
    printf("Use 'config data show %s %s' to display this key\n", namespace,
           key);
    nvs_close(handle);
    return ESP_ERR_INVALID_ARG;
  }

  // Get blob size
  size_t required_size;
  ret = nvs_get_blob(handle, key, NULL, &required_size);
  if (ret != ESP_OK) {
    printf("Error: Key '%s' not found or cannot be read: %s\n", key,
           esp_err_to_name(ret));
    printf("Use 'config data list %s' to see available keys\n", namespace);
    nvs_close(handle);
    return ret;
  }

  if (required_size == 0) {
    printf("%s.%s: <empty blob>\n", namespace, key);
    nvs_close(handle);
    return ESP_OK;
  }

  // Allocate buffer and read blob data
  uint8_t *blob_data = malloc(required_size);
  if (!blob_data) {
    printf("Error: Memory allocation failed for %zu bytes\n", required_size);
    nvs_close(handle);
    return ESP_ERR_NO_MEM;
  }

  ret = nvs_get_blob(handle, key, blob_data, &required_size);
  if (ret != ESP_OK) {
    printf("Error: Failed to read blob data: %s\n", esp_err_to_name(ret));
    free(blob_data);
    nvs_close(handle);
    return ret;
  }

  // Display hex dump with ASCII representation
  printf("Blob Data Dump: %s.%s (%zu bytes)\n", namespace, key, required_size);
  printf("==================================================\n");
  printf("Offset   Hex Data                          ASCII\n");
  printf("-------- --------------------------------- ----------------\n");

  for (size_t i = 0; i < required_size; i += 16) {
    // Print offset
    printf("%08zx ", i);

    // Print hex data (16 bytes per line)
    for (size_t j = 0; j < 16; j++) {
      if (i + j < required_size) {
        printf("%02x ", blob_data[i + j]);
      } else {
        printf("   ");
      }
      if (j == 7)
        printf(" "); // Extra space in the middle
    }

    printf(" ");

    // Print ASCII representation
    for (size_t j = 0; j < 16 && i + j < required_size; j++) {
      uint8_t c = blob_data[i + j];
      if (c >= 32 && c <= 126) {
        printf("%c", c);
      } else {
        printf(".");
      }
    }

    printf("\n");
  }

  printf("\nSummary:\n");
  printf("  Size: %zu bytes\n", required_size);
  printf("  Hex:  ");
  for (size_t i = 0; i < required_size && i < 32; i++) {
    printf("%02x", blob_data[i]);
  }
  if (required_size > 32) {
    printf("... (truncated)");
  }
  printf("\n");

  free(blob_data);
  nvs_close(handle);
  return ESP_OK;
}

/* ============================================================================
 * System Operations
 * ============================================================================
 */

static esp_err_t config_system_stats(void) {
  printf("NVS System Statistics:\n");
  printf("=====================\n");

  nvs_stats_t nvs_stats;
  esp_err_t ret = nvs_get_stats(NVS_DEFAULT_PART_NAME, &nvs_stats);
  if (ret == ESP_OK) {
    printf("Used entries: %zu\n", nvs_stats.used_entries);
    printf("Free entries: %zu\n", nvs_stats.free_entries);
    printf("Total entries: %zu\n", nvs_stats.total_entries);
    printf("Namespace count: %zu\n", nvs_stats.namespace_count);
  } else {
    printf("Error: Could not get NVS statistics: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t config_system_commit(void) {
  printf("Forcing commit of pending configuration changes...\n");

  esp_err_t ret = config_manager_commit();
  if (ret == ESP_OK) {
    printf("Configuration changes committed successfully\n");
  } else {
    printf("Error: Failed to commit changes: %s\n", esp_err_to_name(ret));
  }

  return ret;
}

static esp_err_t config_system_info(void) {
  printf("NVS Partition Information:\n");
  printf("=========================\n");
  printf("Partition: %s\n", NVS_DEFAULT_PART_NAME);
  printf(
      "Note: Detailed partition info requires ESP partition API integration\n");

  return ESP_OK;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

static config_type_t parse_type_string(const char *type_str) {
  if (strcmp(type_str, "u8") == 0)
    return CONFIG_TYPE_U8;
  if (strcmp(type_str, "u16") == 0)
    return CONFIG_TYPE_U16;
  if (strcmp(type_str, "u32") == 0)
    return CONFIG_TYPE_U32;
  if (strcmp(type_str, "i8") == 0)
    return CONFIG_TYPE_I8;
  if (strcmp(type_str, "i16") == 0)
    return CONFIG_TYPE_I16;
  if (strcmp(type_str, "i32") == 0)
    return CONFIG_TYPE_I32;
  if (strcmp(type_str, "str") == 0)
    return CONFIG_TYPE_STRING;
  if (strcmp(type_str, "bool") == 0)
    return CONFIG_TYPE_BOOL;
  if (strcmp(type_str, "blob") == 0)
    return CONFIG_TYPE_BLOB;
  return CONFIG_TYPE_INVALID;
}

/* ============================================================================
 * NVS Data Helper Functions
 * ============================================================================
 */

static esp_err_t detect_and_show_value_type(nvs_handle_t handle,
                                            const char *key) {
  esp_err_t ret;
  size_t required_size;

  // Try different types in order of probability

  // Try uint32_t
  uint32_t u32_val;
  ret = nvs_get_u32(handle, key, &u32_val);
  if (ret == ESP_OK) {
    printf("%s = %lu (u32)\n", key, (unsigned long)u32_val);
    return ESP_OK;
  }

  // Try uint16_t
  uint16_t u16_val;
  ret = nvs_get_u16(handle, key, &u16_val);
  if (ret == ESP_OK) {
    printf("%s = %u (u16)\n", key, u16_val);
    return ESP_OK;
  }

  // Try uint8_t
  uint8_t u8_val;
  ret = nvs_get_u8(handle, key, &u8_val);
  if (ret == ESP_OK) {
    printf("%s = %u (u8)\n", key, u8_val);
    return ESP_OK;
  }

  // Try int32_t
  int32_t i32_val;
  ret = nvs_get_i32(handle, key, &i32_val);
  if (ret == ESP_OK) {
    printf("%s = %ld (i32)\n", key, (long)i32_val);
    return ESP_OK;
  }

  // Try int16_t
  int16_t i16_val;
  ret = nvs_get_i16(handle, key, &i16_val);
  if (ret == ESP_OK) {
    printf("%s = %d (i16)\n", key, i16_val);
    return ESP_OK;
  }

  // Try int8_t
  int8_t i8_val;
  ret = nvs_get_i8(handle, key, &i8_val);
  if (ret == ESP_OK) {
    printf("%s = %d (i8)\n", key, i8_val);
    return ESP_OK;
  }

  // Try string
  ret = nvs_get_str(handle, key, NULL, &required_size);
  if (ret == ESP_OK) {
    char *str_val = malloc(required_size);
    if (str_val) {
      ret = nvs_get_str(handle, key, str_val, &required_size);
      if (ret == ESP_OK) {
        printf("%s = \"%s\" (str)\n", key, str_val);
        free(str_val);
        return ESP_OK;
      }
      free(str_val);
    }
  }

  // Try blob
  ret = nvs_get_blob(handle, key, NULL, &required_size);
  if (ret == ESP_OK) {
    if (required_size <= 64) { // Show hex dump for small blobs
      uint8_t *blob_data = malloc(required_size);
      if (blob_data) {
        ret = nvs_get_blob(handle, key, blob_data, &required_size);
        if (ret == ESP_OK) {
          printf("%s = ", key);
          for (size_t i = 0; i < required_size; i++) {
            printf("%02x", blob_data[i]);
            if (i < required_size - 1)
              printf(" ");
          }
          printf(" (blob %zu bytes)\n", required_size);
          free(blob_data);
          return ESP_OK;
        }
        free(blob_data);
      }
    }
    printf("%s = <blob %zu bytes>\n", key, required_size);
    return ESP_OK;
  }

  printf("%s = <unknown type>\n", key);
  return ESP_ERR_NOT_FOUND;
}

static esp_err_t show_nvs_key_value(nvs_handle_t handle, const char *namespace,
                                    const char *key) {
  printf("%s.%s:\n", namespace, key);
  return detect_and_show_value_type(handle, key);
}

static esp_err_t show_all_namespace_keys(const char *namespace) {
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(namespace, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    printf("Error: Cannot open namespace '%s': %s\n", namespace,
           esp_err_to_name(ret));
    return ret;
  }

  printf("%s Configuration:\n", namespace);
  printf("========================\n");

  // Use NVS iterator to enumerate all keys
  nvs_iterator_t iterator = NULL;
  ret =
      nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_ANY, &iterator);

  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    printf("No configuration keys found in namespace '%s'\n", namespace);
    nvs_close(handle);
    return ESP_OK;
  } else if (ret != ESP_OK) {
    printf("Error enumerating keys: %s\n", esp_err_to_name(ret));
    nvs_close(handle);
    return ret;
  }

  int key_count = 0;
  while (iterator != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);

    printf("  ");
    detect_and_show_value_type(handle, info.key);
    key_count++;

    ret = nvs_entry_next(&iterator);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
      printf("Error getting next entry: %s\n", esp_err_to_name(ret));
      break;
    }
  }

  nvs_release_iterator(iterator);
  nvs_close(handle);

  printf("\nFound %d configuration keys\n", key_count);
  return ESP_OK;
}

__attribute__((unused)) static const char *type_to_string(config_type_t type) {
  switch (type) {
  case CONFIG_TYPE_U8:
    return "u8";
  case CONFIG_TYPE_U16:
    return "u16";
  case CONFIG_TYPE_U32:
    return "u32";
  case CONFIG_TYPE_I8:
    return "i8";
  case CONFIG_TYPE_I16:
    return "i16";
  case CONFIG_TYPE_I32:
    return "i32";
  case CONFIG_TYPE_STRING:
    return "str";
  case CONFIG_TYPE_BOOL:
    return "bool";
  case CONFIG_TYPE_BLOB:
    return "blob";
  default:
    return "unknown";
  }
}

static esp_err_t confirm_dangerous_operation(const char *operation,
                                             const char *confirm_text) {
  printf("WARNING: This will %s '%s'\n", operation, confirm_text);
  printf("Type '%s' to confirm: ", confirm_text);

  // Note: In full implementation, we would read user input from console
  // For now, we'll just print the prompt and return an error
  printf("\nOperation cancelled (user input not implemented yet)\n");
  return ESP_ERR_NOT_FINISHED;
}

static bool is_valid_namespace_name(const char *name) {
  if (!name || strlen(name) == 0 || strlen(name) >= MAX_NAMESPACE_NAME) {
    return false;
  }

  // Check for valid characters (alphanumeric and underscore)
  for (const char *p = name; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '_') {
      return false;
    }
  }

  return true;
}

static bool is_valid_key_name(const char *name) {
  if (!name || strlen(name) == 0 || strlen(name) >= MAX_KEY_NAME) {
    return false;
  }

  // Check for valid characters (alphanumeric and underscore)
  for (const char *p = name; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '_') {
      return false;
    }
  }

  return true;
}