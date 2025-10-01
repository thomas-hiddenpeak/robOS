/**
 * @file ethernet_console.c
 * @brief Ethernet management console commands
 */

#include "ethernet_console.h"
#include "console_core.h"
#include "esp_log.h"
#include "ethernet_manager.h"

static const char *TAG = "ethernet_console";

// Forward declarations
static esp_err_t cmd_net(int argc, char **argv);
static esp_err_t net_cmd_status(int argc, char **argv);
static esp_err_t net_cmd_config(int argc, char **argv);
static esp_err_t net_cmd_reset(int argc, char **argv);
static esp_err_t net_cmd_start(int argc, char **argv);
static esp_err_t net_cmd_stop(int argc, char **argv);
static esp_err_t net_cmd_dhcp(int argc, char **argv);

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
  } else {
    printf("Configuration management is not implemented yet.\n");
    printf("Available actions:\n");
    printf("  net config show    - Show current configuration\n");
    printf("Future commands:\n");
    printf("  net config set <param> <value> - Set network parameter\n");
    printf("  net config save                - Save configuration\n");
    printf("  net config load                - Load configuration\n");
    printf("  net config reset               - Reset to defaults\n");
    return ESP_OK;
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

  printf("Network interface reset successfully\n");
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