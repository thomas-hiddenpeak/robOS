/**
 * @file ethernet_console.c
 * @brief Ethernet management console commands
 */

#include "ethernet_console.h"
#include "console_core.h"
#include "esp_log.h"
#include "ethernet_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ethernet_console";

// Forward declarations
static esp_err_t cmd_net(int argc, char **argv);
static esp_err_t net_cmd_status(int argc, char **argv);
static esp_err_t net_cmd_config(int argc, char **argv);
static esp_err_t net_cmd_reset(int argc, char **argv);
static esp_err_t net_cmd_start(int argc, char **argv);
static esp_err_t net_cmd_stop(int argc, char **argv);
static esp_err_t net_cmd_dhcp(int argc, char **argv);
static esp_err_t net_cmd_log(int argc, char **argv);
static esp_err_t net_config_set_parameter(const char *param, const char *value);
static bool is_valid_ip_address(const char *ip_str);

/* ============================================================================
 * Command Implementation Functions
 * ============================================================================
 */

/**
 * @brief Main net command handler
 */
static esp_err_t cmd_net(int argc, char **argv) {
  if (argc < 2) {
    printf("Network Management Commands:\n");
    printf("  net status                    - Show network interface status\n");
    printf("  net config                    - Show current network "
           "configuration\n");
    printf("  net config set <param> <val> - Set network parameter\n");
    printf("  net config save               - Save configuration\n");
    printf("  net config load               - Load configuration\n");
    printf(
        "  net config reset              - Reset to default configuration\n");
    printf("  net reset                     - Reset network interface\n");
    printf("  net start                     - Start network interface\n");
    printf("  net stop                      - Stop network interface\n");
    printf("  net dhcp                      - Show DHCP server status\n");
    printf("  net dhcp enable               - Enable DHCP server\n");
    printf("  net dhcp disable              - Disable DHCP server\n");
    printf("  net log [options]             - Show network activity log\n");
    return ESP_OK;
  }

  const char *subcmd = argv[1];

  if (strcmp(subcmd, "status") == 0) {
    return net_cmd_status(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "config") == 0) {
    return net_cmd_config(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "reset") == 0) {
    return net_cmd_reset(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "start") == 0) {
    return net_cmd_start(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "stop") == 0) {
    return net_cmd_stop(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "dhcp") == 0) {
    return net_cmd_dhcp(argc - 1, argv + 1);
  } else if (strcmp(subcmd, "log") == 0) {
    return net_cmd_log(argc - 1, argv + 1);
  } else {
    printf("Unknown subcommand: %s\n", subcmd);
    printf("Use 'net' without arguments to see available commands\n");
    return ESP_ERR_INVALID_ARG;
  }
}

/**
 * @brief Show network status
 */
static esp_err_t net_cmd_status(int argc, char **argv) {
  (void)argc; // Unused parameter
  (void)argv; // Unused parameter

  ethernet_manager_status_t status;
  esp_err_t ret = ethernet_manager_get_status(&status);

  if (ret != ESP_OK) {
    printf("Failed to get ethernet status: %s\n", esp_err_to_name(ret));
    return ret;
  }

  // Display interface status
  printf("=== Network Interface Status ===\n");
  printf("Initialized:     %s\n", status.initialized ? "Yes" : "No");
  printf("Started:         %s\n", status.started ? "Yes" : "No");
  printf("Link Status:     %s\n", status.link_up ? "Up" : "Down");
  printf("MAC Address:     %02x:%02x:%02x:%02x:%02x:%02x\n", status.mac_addr[0],
         status.mac_addr[1], status.mac_addr[2], status.mac_addr[3],
         status.mac_addr[4], status.mac_addr[5]);

  // Display IP configuration
  printf("\n=== IP Configuration ===\n");
  printf("IP Address:      %s\n", status.config.network.ip_addr);
  printf("Netmask:         %s\n", status.config.network.netmask);
  printf("Gateway:         %s\n", status.config.network.gateway);
  printf("DNS Server:      %s\n", status.config.network.dns_server);
  printf("DHCP Client:     %s\n",
         status.config.network.dhcp_client_enable ? "Enabled" : "Disabled");

  // Display DHCP Server configuration
  printf("\n=== DHCP Server ===\n");
  printf("Status:          %s\n",
         status.config.dhcp_server.enable ? "Enabled" : "Disabled");
  if (status.config.dhcp_server.enable) {
    printf("Pool Start:      %s\n", status.config.dhcp_server.pool_start);
    printf("Pool End:        %s\n", status.config.dhcp_server.pool_end);
    printf("Lease Time:      %lu hours\n",
           (unsigned long)status.config.dhcp_server.lease_time_hours);
    printf("Max Clients:     %u\n", status.config.dhcp_server.max_clients);
  }

  // Display statistics
  printf("\n=== Statistics ===\n");
  printf("RX Packets:      %lu\n", (unsigned long)status.rx_packets);
  printf("TX Packets:      %lu\n", (unsigned long)status.tx_packets);
  printf("RX Bytes:        %lu\n", (unsigned long)status.rx_bytes);
  printf("TX Bytes:        %lu\n", (unsigned long)status.tx_bytes);
  printf("RX Errors:       %lu\n", (unsigned long)status.rx_errors);
  printf("TX Errors:       %lu\n", (unsigned long)status.tx_errors);

  return ESP_OK;
}

/**
 * @brief Network configuration management
 */
static esp_err_t net_cmd_config(int argc, char **argv) {
  if (argc == 1) {
    // Show current configuration
    return net_cmd_status(0, NULL);
  }

  const char *action = argv[1];

  if (strcmp(action, "show") == 0) {
    return net_cmd_status(0, NULL);
  } else if (strcmp(action, "set") == 0) {
    if (argc < 4) {
      printf("Usage: net config set <param> <value>\n\n");
      printf("Available parameters:\n");
      printf("  ip <x.x.x.x>           - Set static IP address\n");
      printf("  netmask <x.x.x.x>      - Set network mask\n");
      printf("  gateway <x.x.x.x>      - Set gateway address\n");
      printf("  dns <x.x.x.x>          - Set DNS server\n");
      printf("  dhcp_pool_start <x.x.x.x> - Set DHCP pool start IP\n");
      printf("  dhcp_pool_end <x.x.x.x>   - Set DHCP pool end IP\n");
      printf("  dhcp_lease_hours <n>   - Set DHCP lease time in hours\n");
      printf("  dhcp_max_clients <n>   - Set maximum DHCP clients\n");
      printf("\nExamples:\n");
      printf("  net config set ip 10.10.99.97\n");
      printf("  net config set dns 8.8.8.8\n");
      printf("  net config set dhcp_lease_hours 24\n");
      return ESP_OK;
    }

    const char *param = argv[2];
    const char *value = argv[3];

    return net_config_set_parameter(param, value);
  } else if (strcmp(action, "save") == 0) {
    printf("Saving network configuration...\n");
    esp_err_t ret = ethernet_manager_save_config();
    if (ret == ESP_OK) {
      printf("Configuration saved successfully\n");
    } else {
      printf("Failed to save configuration: %s\n", esp_err_to_name(ret));
    }
    return ret;
  } else if (strcmp(action, "load") == 0) {
    printf("Loading network configuration...\n");
    esp_err_t ret = ethernet_manager_load_config();
    if (ret == ESP_OK) {
      printf("Configuration loaded successfully\n");
    } else {
      printf("Failed to load configuration: %s\n", esp_err_to_name(ret));
    }
    return ret;
  } else if (strcmp(action, "reset") == 0) {
    printf("Resetting network configuration to defaults...\n");
    esp_err_t ret = ethernet_manager_reset_config();
    if (ret == ESP_OK) {
      printf("Configuration reset successfully\n");
    } else {
      printf("Failed to reset configuration: %s\n", esp_err_to_name(ret));
    }
    return ret;
  } else {
    printf("Unknown action: %s\n", action);
    printf("Available actions:\n");
    printf("  net config show             - Show current configuration\n");
    printf("  net config set <param> <value> - Set network parameter\n");
    printf("  net config save             - Save configuration\n");
    printf("  net config load             - Load configuration\n");
    printf("  net config reset            - Reset to defaults\n");
    return ESP_ERR_INVALID_ARG;
  }
}

/**
 * @brief Reset network interface
 */
static esp_err_t net_cmd_reset(int argc, char **argv) {
  (void)argc; // Unused parameter
  (void)argv; // Unused parameter

  printf("Resetting network interface...\n");

  esp_err_t ret = ethernet_manager_reset();
  if (ret != ESP_OK) {
    printf("Failed to reset network interface: %s\n", esp_err_to_name(ret));
    return ret;
  }

  printf("Network interface reset completed\n");
  printf("\n");
  printf("NOTE: If IP configuration failed to apply dynamically,\n");
  printf("the configuration has been saved and will be applied\n");
  printf("on the next system restart.\n");
  printf("\n");
  printf("To ensure all changes take effect, consider using:\n");
  printf("  system reboot\n");
  return ESP_OK;
}

/**
 * @brief Start network interface
 */
static esp_err_t net_cmd_start(int argc, char **argv) {
  (void)argc; // Unused parameter
  (void)argv; // Unused parameter

  printf("Starting network interface...\n");
  esp_err_t ret = ethernet_manager_start();
  if (ret == ESP_OK) {
    printf("Network interface started successfully\n");
  } else {
    printf("Failed to start network interface: %s\n", esp_err_to_name(ret));
  }
  return ret;
}

/**
 * @brief Stop network interface
 */
static esp_err_t net_cmd_stop(int argc, char **argv) {
  (void)argc; // Unused parameter
  (void)argv; // Unused parameter

  printf("Stopping network interface...\n");
  esp_err_t ret = ethernet_manager_stop();
  if (ret == ESP_OK) {
    printf("Network interface stopped successfully\n");
  } else {
    printf("Failed to stop network interface: %s\n", esp_err_to_name(ret));
  }
  return ret;
}

/**
 * @brief DHCP server management
 */
static esp_err_t net_cmd_dhcp(int argc, char **argv) {
  if (argc < 2) {
    // Show DHCP status
    ethernet_manager_status_t status;
    esp_err_t ret = ethernet_manager_get_status(&status);
    if (ret != ESP_OK) {
      printf("Failed to get network status\n");
      return ret;
    }

    printf("DHCP Server Status: %s\n",
           status.config.dhcp_server.enable ? "Enabled" : "Disabled");
    if (status.config.dhcp_server.enable) {
      printf("Pool Start:   %s\n", status.config.dhcp_server.pool_start);
      printf("Pool End:     %s\n", status.config.dhcp_server.pool_end);
      printf("Lease Time:   %lu hours\n",
             (unsigned long)status.config.dhcp_server.lease_time_hours);
      printf("Max Clients:  %u\n", status.config.dhcp_server.max_clients);
    }
    return ESP_OK;
  }

  const char *action = argv[1];
  if (strcmp(action, "enable") == 0) {
    printf("Enabling DHCP server...\n");
    esp_err_t ret = ethernet_manager_dhcp_server_start();
    if (ret == ESP_OK) {
      printf("DHCP server enabled successfully\n");
    } else {
      printf("Failed to enable DHCP server: %s\n", esp_err_to_name(ret));
    }
    return ret;
  } else if (strcmp(action, "disable") == 0) {
    printf("Disabling DHCP server...\n");
    esp_err_t ret = ethernet_manager_dhcp_server_stop();
    if (ret == ESP_OK) {
      printf("DHCP server disabled successfully\n");
    } else {
      printf("Failed to disable DHCP server: %s\n", esp_err_to_name(ret));
    }
    return ret;
  } else {
    printf("Unknown DHCP action: %s\n", action);
    printf("Usage: net dhcp [enable|disable]\n");
    return ESP_ERR_INVALID_ARG;
  }
}

/**
 * @brief Show network activity log
 */
static esp_err_t net_cmd_log(int argc, char **argv) {
  printf("=== Network Activity Log ===\n");

  // Parse options
  uint32_t max_entries = 16; // Default to show 16 entries
  bool show_all = false;
  bool verbose = false;

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "all") == 0) {
      show_all = true;
      max_entries = 32; // Show all stored entries
    } else if (strcmp(argv[i], "verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      verbose = true;
    } else if (strncmp(argv[i], "count=", 6) == 0) {
      int count = atoi(argv[i] + 6);
      if (count > 0 && count <= 32) {
        max_entries = (uint32_t)count;
      }
    } else if (strcmp(argv[i], "help") == 0) {
      printf("Usage: net log [options]\n");
      printf("Options:\n");
      printf("  all              - Show all stored entries (up to 32)\n");
      printf("  verbose, -v      - Show detailed information\n");
      printf("  count=N          - Show N most recent entries (1-32)\n");
      printf("  help             - Show this help\n");
      printf("\nExamples:\n");
      printf("  net log          - Show 16 most recent entries\n");
      printf("  net log all      - Show all stored entries\n");
      printf("  net log verbose  - Show detailed information\n");
      printf("  net log count=5  - Show 5 most recent entries\n");
      return ESP_OK;
    }
  }

  char entries[32][128]; // Get up to 32 entries
  uint32_t total_entries;
  uint32_t retrieved =
      ethernet_manager_get_activity_log(entries, max_entries, &total_entries);

  if (retrieved == 0) {
    printf("No network activity recorded yet.\n");
    printf("\nUse 'net log help' for usage information.\n");
    return ESP_OK;
  }

  if (show_all) {
    printf("Showing all %u stored entries (total recorded: %u)\n\n",
           (unsigned int)retrieved, (unsigned int)total_entries);
  } else {
    printf("Showing %u most recent entries (total recorded: %u)\n",
           (unsigned int)retrieved, (unsigned int)total_entries);
    if (total_entries > retrieved) {
      printf("Use 'net log all' to see all stored entries.\n");
    }
    printf("\n");
  }

  for (uint32_t i = 0; i < retrieved; i++) {
    if (verbose) {
      printf("[%02u] %s\n", (unsigned int)(retrieved - i), entries[i]);
    } else {
      printf("%s\n", entries[i]);
    }
  }

  if (verbose) {
    printf("\n=== System Information ===\n");
    printf("Log buffer size: 32 entries\n");
    printf("Entry format: HH:MM:SS - Activity Description\n");
    printf("Time format: Hours:Minutes:Seconds since boot\n");
    printf("Oldest entries are automatically overwritten.\n");
    printf("Current system uptime: ");

    // Get current uptime for reference
    uint32_t uptime_sec = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    uint32_t up_hours = uptime_sec / 3600;
    uint32_t up_minutes = (uptime_sec % 3600) / 60;
    uint32_t up_secs = uptime_sec % 60;
    printf("%02lu:%02lu:%02lu\n", (unsigned long)up_hours,
           (unsigned long)up_minutes, (unsigned long)up_secs);
  }

  printf("\nTip: Use 'net log help' for more options.\n");
  printf("     Use 'net status' for current network state.\n");

  return ESP_OK;
}

/**
 * @brief Set a network configuration parameter
 */
static esp_err_t net_config_set_parameter(const char *param,
                                          const char *value) {
  if (!param || !value) {
    printf("Error: Invalid parameter or value\n");
    return ESP_ERR_INVALID_ARG;
  }

  printf("Setting network parameter '%s' to '%s'...\n", param, value);

  // Get current configuration
  ethernet_manager_status_t status;
  esp_err_t ret = ethernet_manager_get_status(&status);
  if (ret != ESP_OK) {
    printf("Failed to get current configuration: %s\n", esp_err_to_name(ret));
    return ret;
  }

  // Parse and validate different parameters
  if (strcmp(param, "ip") == 0) {
    if (!is_valid_ip_address(value)) {
      printf("Error: Invalid IP address format. Use x.x.x.x format\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_ip_address(value);
  } else if (strcmp(param, "netmask") == 0) {
    if (!is_valid_ip_address(value)) {
      printf("Error: Invalid netmask format. Use x.x.x.x format\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_netmask(value);
  } else if (strcmp(param, "gateway") == 0) {
    if (!is_valid_ip_address(value)) {
      printf("Error: Invalid gateway address format. Use x.x.x.x format\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_gateway(value);
  } else if (strcmp(param, "dns") == 0) {
    if (!is_valid_ip_address(value)) {
      printf("Error: Invalid DNS server address format. Use x.x.x.x format\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_dns_server(value);
  } else if (strcmp(param, "dhcp_pool_start") == 0) {
    if (!is_valid_ip_address(value)) {
      printf("Error: Invalid DHCP pool start address format. Use x.x.x.x "
             "format\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_dhcp_pool_start(value);
  } else if (strcmp(param, "dhcp_pool_end") == 0) {
    if (!is_valid_ip_address(value)) {
      printf(
          "Error: Invalid DHCP pool end address format. Use x.x.x.x format\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_dhcp_pool_end(value);
  } else if (strcmp(param, "dhcp_lease_hours") == 0) {
    int hours = atoi(value);
    if (hours <= 0 || hours > 8760) { // Max 1 year
      printf("Error: Invalid lease time. Must be between 1 and 8760 hours\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_dhcp_lease_time(hours);
  } else if (strcmp(param, "dhcp_max_clients") == 0) {
    int max_clients = atoi(value);
    if (max_clients <= 0 || max_clients > 50) {
      printf("Error: Invalid max clients. Must be between 1 and 50\n");
      return ESP_ERR_INVALID_ARG;
    }
    ret = ethernet_manager_set_dhcp_max_clients(max_clients);
  } else {
    printf("Error: Unknown parameter '%s'\n", param);
    printf(
        "Use 'net config set' without arguments to see available parameters\n");
    return ESP_ERR_INVALID_ARG;
  }

  if (ret == ESP_OK) {
    printf("Parameter '%s' set successfully\n", param);
    printf("Note: Use 'net config save' to persist this configuration\n");
    printf("      Use 'net reset' to apply changes to running interface\n");
  } else {
    printf("Failed to set parameter '%s': %s\n", param, esp_err_to_name(ret));
  }

  return ret;
}

/**
 * @brief Validate IP address format
 */
static bool is_valid_ip_address(const char *ip_str) {
  if (!ip_str)
    return false;

  int a, b, c, d;
  if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }

  return (a >= 0 && a <= 255) && (b >= 0 && b <= 255) && (c >= 0 && c <= 255) &&
         (d >= 0 && d <= 255);
}

/* ============================================================================
 * Command Registration
 * ============================================================================
 */

esp_err_t ethernet_console_init(void) {
  ESP_LOGI(TAG, "Registering network console commands...");

  // Register main net command
  console_cmd_t net_cmd = {.command = "net",
                           .help = "Network management commands",
                           .hint = "[status|config|reset|start|stop|dhcp]",
                           .func = cmd_net,
                           .min_args = 0,
                           .max_args = 4};

  esp_err_t ret = console_register_command(&net_cmd);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Network console commands registered successfully");
  } else {
    ESP_LOGE(TAG, "Failed to register network console commands");
  }

  return ret;
}

esp_err_t ethernet_console_deinit(void) {
  ESP_LOGI(TAG, "Unregistering network console commands...");

  esp_err_t ret = console_unregister_command("net");

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Network console commands unregistered successfully");
  } else {
    ESP_LOGE(TAG, "Failed to unregister network console commands");
  }

  return ret;
}