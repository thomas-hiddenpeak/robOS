/**
 * @file ethernet_console.h
 * @brief Ethernet Manager Console Commands Interface
 *
 * This header defines the console command interface for the ethernet manager
 * component. It provides command registration and implementation functions for
 * network management.
 *
 * @author robOS Team
 * @date 2025
 */

#ifndef ETHERNET_CONSOLE_H
#define ETHERNET_CONSOLE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Console Command Registration
 * ============================================================================
 */

/**
 * @brief Initialize ethernet console commands
 *
 * This function registers the main 'net' command with console_core:
 * - net status: Display network interface status
 * - net config: Network configuration management
 * - net reset: Reset network interface
 * - net start: Start network interface
 * - net stop: Stop network interface
 * - net dhcp: DHCP server management
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_console_init(void);

/**
 * @brief Deinitialize ethernet console commands
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_console_deinit(void);

/* ============================================================================
 * Command Implementation Functions
 * ============================================================================
 */

/**
 * @brief Ethernet status command implementation
 *
 * Usage: eth-status
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return esp_err_t Command execution result
 */
esp_err_t ethernet_console_cmd_status(int argc, char **argv);

/**
 * @brief Ethernet configuration command implementation
 *
 * Usage:
 *   eth-config                           - Show current configuration
 *   eth-config set ip <ip_address>       - Set IP address
 *   eth-config set gateway <gateway>     - Set gateway address
 *   eth-config set netmask <netmask>     - Set subnet mask
 *   eth-config set dns <dns_server>      - Set DNS server
 *   eth-config save                      - Save configuration
 *   eth-config load                      - Load configuration
 *   eth-config reset                     - Reset to defaults
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return esp_err_t Command execution result
 */
esp_err_t ethernet_console_cmd_config(int argc, char **argv);

/**
 * @brief Ethernet reset command implementation
 *
 * Usage: eth-reset
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return esp_err_t Command execution result
 */
esp_err_t ethernet_console_cmd_reset(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // ETHERNET_CONSOLE_H