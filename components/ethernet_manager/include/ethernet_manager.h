/**
 * @file ethernet_manager.h
 * @brief Ethernet Manager Component for robOS
 *
 * This component provides comprehensive ethernet management for the robOS
 * system, including W5500 hardware control, network configuration, DHCP server,
 * and console interface. Designed to integrate with robOS's modular
 * architecture.
 *
 * Hardware Configuration:
 * - W5500 Ethernet Controller (SPI2_HOST)
 * - RST: GPIO 39 - Reset signal
 * - INT: GPIO 38 - Interrupt signal
 * - MISO: GPIO 13 - SPI data input
 * - SCLK: GPIO 12 - SPI clock
 * - MOSI: GPIO 11 - SPI data output
 * - CS: GPIO 10 - SPI chip select
 *
 * @author robOS Team
 * @date 2025
 */

#ifndef ETHERNET_MANAGER_H
#define ETHERNET_MANAGER_H

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Configuration
 * ============================================================================
 */

#define ETHERNET_MANAGER_DEFAULT_IP "10.10.99.97"
#define ETHERNET_MANAGER_DEFAULT_GATEWAY "10.10.99.1"
#define ETHERNET_MANAGER_DEFAULT_NETMASK "255.255.255.0"
#define ETHERNET_MANAGER_DEFAULT_DNS "8.8.8.8"

#define ETHERNET_MANAGER_DHCP_POOL_START "10.10.99.100"
#define ETHERNET_MANAGER_DHCP_POOL_END "10.10.99.110"
#define ETHERNET_MANAGER_DHCP_LEASE_TIME 24 // hours

/* ============================================================================
 * Data Types and Structures
 * ============================================================================
 */

/**
 * @brief Ethernet manager status enumeration
 */
typedef enum {
  ETHERNET_STATUS_UNINITIALIZED = 0, ///< Component not initialized
  ETHERNET_STATUS_INITIALIZED,       ///< Initialized but not started
  ETHERNET_STATUS_STARTING,          ///< Starting up
  ETHERNET_STATUS_DISCONNECTED,      ///< Hardware disconnected
  ETHERNET_STATUS_CONNECTED,         ///< Hardware connected
  ETHERNET_STATUS_IP_ASSIGNED,       ///< IP address assigned
  ETHERNET_STATUS_READY              ///< Fully operational
} ethernet_status_t;

/**
 * @brief Ethernet network configuration structure
 */
typedef struct {
  char ip_addr[16];        ///< IP address string (e.g., "10.10.99.97")
  char gateway[16];        ///< Gateway address string
  char netmask[16];        ///< Subnet mask string
  char dns_server[16];     ///< DNS server address string
  bool dhcp_client_enable; ///< Enable DHCP client mode
  bool auto_start;         ///< Auto start on initialization
} ethernet_network_config_t;

/**
 * @brief DHCP server configuration structure
 */
typedef struct {
  bool enable;               ///< Enable DHCP server
  char pool_start[16];       ///< DHCP pool start IP
  char pool_end[16];         ///< DHCP pool end IP
  uint32_t lease_time_hours; ///< Lease time in hours
  uint8_t max_clients;       ///< Maximum clients
  bool auto_start;           ///< Auto start DHCP server
} ethernet_dhcp_config_t;

/**
 * @brief Complete ethernet manager configuration
 */
typedef struct {
  ethernet_network_config_t network;  ///< Network configuration
  ethernet_dhcp_config_t dhcp_server; ///< DHCP server configuration
} ethernet_manager_config_t;

/**
 * @brief Ethernet manager status information
 */
typedef struct {
  bool initialized;                 ///< Initialization status
  bool started;                     ///< Started status
  ethernet_status_t status;         ///< Current status
  ethernet_manager_config_t config; ///< Current configuration

  // Hardware status
  bool link_up;        ///< Physical link status
  uint8_t mac_addr[6]; ///< MAC address

  // Network statistics
  uint32_t rx_packets; ///< Received packets
  uint32_t tx_packets; ///< Transmitted packets
  uint32_t rx_bytes;   ///< Received bytes
  uint32_t tx_bytes;   ///< Transmitted bytes
  uint32_t rx_errors;  ///< Receive errors
  uint32_t tx_errors;  ///< Transmit errors
} ethernet_manager_status_t;

/**
 * @brief Ethernet event callback function type
 *
 * @param status Current ethernet status
 * @param user_data User data passed to callback
 */
typedef void (*ethernet_event_callback_t)(ethernet_status_t status,
                                          void *user_data);

/* ============================================================================
 * Default Configuration Macros
 * ============================================================================
 */

/**
 * @brief Default network configuration
 */
#define ETHERNET_DEFAULT_NETWORK_CONFIG()                                      \
  {.ip_addr = ETHERNET_MANAGER_DEFAULT_IP,                                     \
   .gateway = ETHERNET_MANAGER_DEFAULT_GATEWAY,                                \
   .netmask = ETHERNET_MANAGER_DEFAULT_NETMASK,                                \
   .dns_server = ETHERNET_MANAGER_DEFAULT_DNS,                                 \
   .dhcp_client_enable = false,                                                \
   .auto_start = true}

/**
 * @brief Default DHCP server configuration
 */
#define ETHERNET_DEFAULT_DHCP_CONFIG()                                         \
  {.enable = false,                                                            \
   .pool_start = ETHERNET_MANAGER_DHCP_POOL_START,                             \
   .pool_end = ETHERNET_MANAGER_DHCP_POOL_END,                                 \
   .lease_time_hours = ETHERNET_MANAGER_DHCP_LEASE_TIME,                       \
   .max_clients = 10,                                                          \
   .auto_start = false}

/**
 * @brief Default ethernet manager configuration
 */
#define ETHERNET_DEFAULT_CONFIG()                                              \
  {.network = ETHERNET_DEFAULT_NETWORK_CONFIG(),                               \
   .dhcp_server = ETHERNET_DEFAULT_DHCP_CONFIG()}

/* ============================================================================
 * Core Management Functions
 * ============================================================================
 */

/**
 * @brief Initialize ethernet manager
 *
 * @param config Configuration structure (NULL for default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_init(const ethernet_manager_config_t *config);

/**
 * @brief Deinitialize ethernet manager
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_deinit(void);

/**
 * @brief Check if ethernet manager is initialized
 *
 * @return true if initialized, false otherwise
 */
bool ethernet_manager_is_initialized(void);

/**
 * @brief Start ethernet manager
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_start(void);

/**
 * @brief Stop ethernet manager
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_stop(void);

/**
 * @brief Get ethernet manager status
 *
 * @param status Pointer to status structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_get_status(ethernet_manager_status_t *status);

/* ============================================================================
 * Configuration Functions
 * ============================================================================
 */

/**
 * @brief Set network configuration
 *
 * @param config Network configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t
ethernet_manager_set_network_config(const ethernet_network_config_t *config);

/**
 * @brief Get network configuration
 *
 * @param config Pointer to store network configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t
ethernet_manager_get_network_config(ethernet_network_config_t *config);

/**
 * @brief Set DHCP server configuration
 *
 * @param config DHCP server configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t
ethernet_manager_set_dhcp_config(const ethernet_dhcp_config_t *config);

/**
 * @brief Get DHCP server configuration
 *
 * @param config Pointer to store DHCP server configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_get_dhcp_config(ethernet_dhcp_config_t *config);

/* ============================================================================
 * Individual Parameter Configuration Functions
 * ============================================================================
 */

/**
 * @brief Set IP address
 */
esp_err_t ethernet_manager_set_ip_address(const char *ip_addr);

/**
 * @brief Set network mask
 */
esp_err_t ethernet_manager_set_netmask(const char *netmask);

/**
 * @brief Set gateway address
 */
esp_err_t ethernet_manager_set_gateway(const char *gateway);

/**
 * @brief Set DNS server address
 */
esp_err_t ethernet_manager_set_dns_server(const char *dns_server);

/**
 * @brief Set DHCP pool start address
 */
esp_err_t ethernet_manager_set_dhcp_pool_start(const char *pool_start);

/**
 * @brief Set DHCP pool end address
 */
esp_err_t ethernet_manager_set_dhcp_pool_end(const char *pool_end);

/**
 * @brief Set DHCP lease time in hours
 */
esp_err_t ethernet_manager_set_dhcp_lease_time(uint32_t hours);

/**
 * @brief Set maximum DHCP clients
 */
esp_err_t ethernet_manager_set_dhcp_max_clients(uint8_t max_clients);

/**
 * @brief Save configuration to NVS
 */
esp_err_t ethernet_manager_save_config(void);

/**
 * @brief Load configuration from NVS
 */
esp_err_t ethernet_manager_load_config(void);

/**
 * @brief Reset configuration to defaults
 */
esp_err_t ethernet_manager_reset_config(void);

/* ============================================================================
 * Network Control Functions
 * ============================================================================
 */

/**
 * @brief Reset ethernet interface
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_reset(void);

/**
 * @brief Check if ethernet cable is connected
 *
 * @return true if connected, false otherwise
 */
bool ethernet_manager_is_link_up(void);

/**
 * @brief Get MAC address
 *
 * @param mac_addr Buffer to store MAC address (6 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_get_mac_address(uint8_t *mac_addr);

/**
 * @brief Start DHCP server
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_dhcp_server_start(void);

/**
 * @brief Stop DHCP server
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_dhcp_server_stop(void);

/**
 * @brief Get network activity log
 *
 * @param entries Buffer to store log entries (caller allocated)
 * @param max_entries Maximum number of entries to retrieve
 * @param total_entries Pointer to store total number of entries available
 * @return Number of entries actually retrieved
 */
uint32_t ethernet_manager_get_activity_log(char entries[][128],
                                           uint32_t max_entries,
                                           uint32_t *total_entries);

/* ============================================================================
 * Event System Integration
 * ============================================================================
 */

/**
 * @brief Register event callback
 *
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t
ethernet_manager_register_event_callback(ethernet_event_callback_t callback,
                                         void *user_data);

/* ============================================================================
 * Console Integration
 * ============================================================================
 */

/**
 * @brief Register ethernet console commands
 *
 * This function registers all ethernet-related console commands with
 * console_core. Must be called after console_core_init() and
 * ethernet_manager_init().
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_register_console_commands(void);

/**
 * @brief Unregister ethernet console commands
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_manager_unregister_console_commands(void);

#ifdef __cplusplus
}
#endif

#endif // ETHERNET_MANAGER_H