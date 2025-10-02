/**
 * @file ethernet_manager.c
 * @brief Ethernet Manager Component Implementation
 *
 * This file implements the core ethernet management functionality for robOS,
 * including W5500 hardware control, network configuration, and integration
 * with the robOS modular architecture.
 *
 * @author robOS Team
 * @date 2025
 */

#include "ethernet_manager.h"
#include "apps/dhcpserver/dhcpserver.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_system.h"
#include "ethernet_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Component Integration Includes
 * ============================================================================
 */
#include "config_manager.h"
#include "event_manager.h"

static const char *TAG = "ETHERNET_MANAGER";

/* ============================================================================
 * Hardware Configuration (W5500)
 * ============================================================================
 */

// W5500 SPI Configuration
#define W5500_SPI_HOST SPI2_HOST
#define W5500_RST_GPIO GPIO_NUM_39
#define W5500_INT_GPIO GPIO_NUM_38
#define W5500_MISO_GPIO GPIO_NUM_13
#define W5500_MOSI_GPIO GPIO_NUM_11
#define W5500_SCLK_GPIO GPIO_NUM_12
#define W5500_CS_GPIO GPIO_NUM_10
#define W5500_SPI_CLOCK_MHZ 20
#define W5500_SPI_QUEUE_SIZE 20

/* ============================================================================
 * Internal State Management
 * ============================================================================
 */

/**
 * @brief Internal ethernet manager state
 */
typedef struct {
  bool initialized;                 ///< Initialization flag
  bool started;                     ///< Started flag
  ethernet_status_t status;         ///< Current status
  ethernet_manager_config_t config; ///< Current configuration
  SemaphoreHandle_t mutex;          ///< Thread safety mutex

  // ESP-IDF ethernet components
  esp_netif_t *netif;          ///< Network interface
  esp_eth_handle_t eth_handle; ///< Ethernet handle
  esp_eth_mac_t *mac;          ///< MAC layer
  esp_eth_phy_t *phy;          ///< PHY layer

  // Hardware state
  bool link_up;        ///< Physical link status
  uint8_t mac_addr[6]; ///< MAC address

  // Statistics
  uint32_t rx_packets; ///< Received packets
  uint32_t tx_packets; ///< Transmitted packets
  uint32_t rx_bytes;   ///< Received bytes
  uint32_t tx_bytes;   ///< Transmitted bytes
  uint32_t rx_errors;  ///< Receive errors
  uint32_t tx_errors;  ///< Transmit errors

  // Event callback
  ethernet_event_callback_t event_callback; ///< Event callback function
  void *event_user_data;                    ///< Event callback user data

  // Network activity log
  struct {
    char entries[32][128];  ///< Log entries (32 entries, 128 chars each)
    uint8_t current_index;  ///< Current entry index (circular buffer)
    uint32_t total_entries; ///< Total number of entries logged
  } activity_log;

  // DHCP monitoring
  struct {
    uint32_t last_assigned_ip; ///< Last assigned IP (for change detection)
    uint32_t client_count;     ///< Number of connected clients
  } dhcp_monitor;
} ethernet_manager_state_t;

// Global state instance
static ethernet_manager_state_t s_ethernet_state = {0};

/* ============================================================================
 * Forward Declarations
 * ============================================================================
 */

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

// IP address conversion utilities
static uint32_t ip_string_to_uint32(const char *ip_str) {
  if (!ip_str)
    return 0;

  int a, b, c, d;
  if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return 0;
  }

  return ((uint32_t)a) | (((uint32_t)b) << 8) | (((uint32_t)c) << 16) |
         (((uint32_t)d) << 24);
}

static esp_err_t ethernet_hw_init(void);
static esp_err_t ethernet_hw_deinit(void);
static esp_err_t ethernet_netif_init(void);
static esp_err_t ethernet_netif_deinit(void);
static void ethernet_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data);
static esp_err_t ethernet_load_config_from_storage(void);
static esp_err_t ethernet_save_config_to_storage(void);
static void ethernet_notify_status_change(ethernet_status_t new_status);
static void ethernet_log_network_activity(const char *activity);
static void ethernet_dhcp_monitor_task(void *params);

/* ============================================================================
 * Core Management Functions
 * ============================================================================
 */

esp_err_t ethernet_manager_init(const ethernet_manager_config_t *config) {
  esp_err_t ret = ESP_OK;

  // Check if already initialized
  if (s_ethernet_state.initialized) {
    ESP_LOGW(TAG, "Ethernet manager already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing ethernet manager...");

  // Reduce lwip DHCP log verbosity to avoid console noise
  esp_log_level_set("esp_netif_lwip", ESP_LOG_WARN);

  // Initialize state structure
  memset(&s_ethernet_state, 0, sizeof(ethernet_manager_state_t));

  // Create mutex for thread safety
  s_ethernet_state.mutex = xSemaphoreCreateMutex();
  if (s_ethernet_state.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }

  // Set configuration (use default if NULL)
  if (config != NULL) {
    memcpy(&s_ethernet_state.config, config, sizeof(ethernet_manager_config_t));
  } else {
    ethernet_manager_config_t default_config = ETHERNET_DEFAULT_CONFIG();
    memcpy(&s_ethernet_state.config, &default_config,
           sizeof(ethernet_manager_config_t));
  }

  // Try to load configuration from storage
  esp_err_t load_ret = ethernet_load_config_from_storage();
  if (load_ret == ESP_OK) {
    ESP_LOGI(TAG, "Configuration loaded from storage");
  } else {
    ESP_LOGI(TAG, "Using default configuration");
  }

  // Initialize hardware
  ret = ethernet_hw_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize ethernet hardware: %s",
             esp_err_to_name(ret));
    goto cleanup;
  }

  // Initialize network interface
  ret = ethernet_netif_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize network interface: %s",
             esp_err_to_name(ret));
    goto cleanup;
  }

  // Set initial status
  s_ethernet_state.status = ETHERNET_STATUS_INITIALIZED;
  s_ethernet_state.initialized = true;

  ESP_LOGI(TAG, "Ethernet manager initialized successfully");
  ethernet_notify_status_change(ETHERNET_STATUS_INITIALIZED);

  return ESP_OK;

cleanup:
  if (s_ethernet_state.mutex) {
    vSemaphoreDelete(s_ethernet_state.mutex);
    s_ethernet_state.mutex = NULL;
  }
  memset(&s_ethernet_state, 0, sizeof(ethernet_manager_state_t));
  return ret;
}

esp_err_t ethernet_manager_deinit(void) {
  if (!s_ethernet_state.initialized) {
    ESP_LOGW(TAG, "Ethernet manager not initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Deinitializing ethernet manager...");

  // Stop if running
  if (s_ethernet_state.started) {
    ethernet_manager_stop();
  }

  // Deinitialize network interface
  ethernet_netif_deinit();

  // Deinitialize hardware
  ethernet_hw_deinit();

  // Cleanup mutex
  if (s_ethernet_state.mutex) {
    vSemaphoreDelete(s_ethernet_state.mutex);
  }

  // Clear state
  memset(&s_ethernet_state, 0, sizeof(ethernet_manager_state_t));

  ESP_LOGI(TAG, "Ethernet manager deinitialized");
  return ESP_OK;
}

bool ethernet_manager_is_initialized(void) {
  return s_ethernet_state.initialized;
}

esp_err_t ethernet_manager_start(void) {
  if (!s_ethernet_state.initialized) {
    ESP_LOGE(TAG, "Ethernet manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (s_ethernet_state.started) {
    ESP_LOGW(TAG, "Ethernet manager already started");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Starting ethernet manager...");

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);

  // Start ethernet driver
  esp_err_t ret = esp_eth_start(s_ethernet_state.eth_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start ethernet: %s", esp_err_to_name(ret));
    xSemaphoreGive(s_ethernet_state.mutex);
    return ret;
  }

  s_ethernet_state.started = true;
  s_ethernet_state.status = ETHERNET_STATUS_STARTING;

  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "Ethernet manager started");
  ethernet_notify_status_change(ETHERNET_STATUS_STARTING);

  // Log DHCP service status after start (since it auto-starts during init)
  if (s_ethernet_state.config.dhcp_server.enable) {
    char dhcp_startup_msg[128];
    snprintf(
        dhcp_startup_msg, sizeof(dhcp_startup_msg),
        "DHCP service active - Pool: %s-%s, DNS: %s, Lease: %luh",
        s_ethernet_state.config.dhcp_server.pool_start,
        s_ethernet_state.config.dhcp_server.pool_end,
        s_ethernet_state.config.network.dns_server,
        (unsigned long)s_ethernet_state.config.dhcp_server.lease_time_hours);
    ethernet_log_network_activity(dhcp_startup_msg);

    // Start DHCP client monitoring task
    BaseType_t task_ret = xTaskCreate(ethernet_dhcp_monitor_task,
                                      "dhcp_monitor", 3072, NULL, 5, NULL);
    if (task_ret != pdPASS) {
      ESP_LOGW(TAG, "Failed to create DHCP monitor task");
    } else {
      ESP_LOGI(TAG, "DHCP client monitor task started");
    }
  }

  return ESP_OK;
}

esp_err_t ethernet_manager_stop(void) {
  if (!s_ethernet_state.initialized || !s_ethernet_state.started) {
    ESP_LOGW(TAG, "Ethernet manager not started");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Stopping ethernet manager...");

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);

  // Stop ethernet driver
  esp_err_t ret = esp_eth_stop(s_ethernet_state.eth_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to stop ethernet: %s", esp_err_to_name(ret));
  }

  s_ethernet_state.started = false;
  s_ethernet_state.status = ETHERNET_STATUS_INITIALIZED;
  s_ethernet_state.link_up = false;

  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "Ethernet manager stopped");
  ethernet_notify_status_change(ETHERNET_STATUS_INITIALIZED);

  return ESP_OK;
}

esp_err_t ethernet_manager_get_status(ethernet_manager_status_t *status) {
  if (!status) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_ethernet_state.initialized) {
    memset(status, 0, sizeof(ethernet_manager_status_t));
    status->status = ETHERNET_STATUS_UNINITIALIZED;
    return ESP_OK;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);

  // Fill status structure
  status->initialized = s_ethernet_state.initialized;
  status->started = s_ethernet_state.started;
  status->status = s_ethernet_state.status;
  memcpy(&status->config, &s_ethernet_state.config,
         sizeof(ethernet_manager_config_t));

  // Hardware status
  status->link_up = s_ethernet_state.link_up;
  memcpy(status->mac_addr, s_ethernet_state.mac_addr, 6);

  // Statistics
  status->rx_packets = s_ethernet_state.rx_packets;
  status->tx_packets = s_ethernet_state.tx_packets;
  status->rx_bytes = s_ethernet_state.rx_bytes;
  status->tx_bytes = s_ethernet_state.tx_bytes;
  status->rx_errors = s_ethernet_state.rx_errors;
  status->tx_errors = s_ethernet_state.tx_errors;

  xSemaphoreGive(s_ethernet_state.mutex);

  return ESP_OK;
}

/* ============================================================================
 * Hardware Initialization (Stub Implementation)
 * ============================================================================
 */

static esp_err_t ethernet_hw_init(void) {
  esp_err_t ret = ESP_OK;

  ESP_LOGI(TAG, "Initializing W5500 hardware...");

  // Configure SPI bus for W5500
  spi_bus_config_t buscfg = {
      .miso_io_num = W5500_MISO_GPIO,
      .mosi_io_num = W5500_MOSI_GPIO,
      .sclk_io_num = W5500_SCLK_GPIO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 0,
  };

  ESP_LOGI(TAG, "SPI pins - MOSI:%d, MISO:%d, SCLK:%d, CS:%d", W5500_MOSI_GPIO,
           W5500_MISO_GPIO, W5500_SCLK_GPIO, W5500_CS_GPIO);

  ret = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "SPI bus initialized successfully");

  // Configure reset pin (output)
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << W5500_RST_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure reset pin %d: %s", W5500_RST_GPIO,
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Reset pin %d configured", W5500_RST_GPIO);

  // Configure interrupt pin (input with pullup)
  io_conf.pin_bit_mask = (1ULL << W5500_INT_GPIO);
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure interrupt pin %d: %s", W5500_INT_GPIO,
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Interrupt pin %d configured", W5500_INT_GPIO);

  // Install GPIO ISR service for W5500 interrupt
  ret = gpio_install_isr_service(0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "GPIO ISR service installed");

  // Reset W5500 with proper timing
  ESP_LOGI(TAG, "Resetting W5500...");
  gpio_set_level(W5500_RST_GPIO, 0);
  vTaskDelay(pdMS_TO_TICKS(100)); // Hold reset for 100ms
  gpio_set_level(W5500_RST_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(500)); // Wait 500ms for chip to boot
  ESP_LOGI(TAG, "W5500 reset complete");

  // Configure SPI device for W5500
  spi_device_interface_config_t spi_devcfg = {
      .command_bits = 16,
      .address_bits = 8,
      .mode = 0,
      .clock_speed_hz = 12 * 1000 * 1000, // 12MHz SPI clock
      .spics_io_num = W5500_CS_GPIO,
      .queue_size = 20,
  };

  // Create W5500 ethernet MAC and PHY
  eth_w5500_config_t w5500_config =
      ETH_W5500_DEFAULT_CONFIG(W5500_SPI_HOST, &spi_devcfg);
  w5500_config.int_gpio_num = W5500_INT_GPIO;

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.reset_gpio_num = W5500_RST_GPIO;
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

  // Create ethernet handle
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  ret = esp_eth_driver_install(&eth_config, &s_ethernet_state.eth_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install ethernet driver: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Set a unique MAC address for W5500 (it doesn't have one built-in)
  uint8_t mac_addr[6] = {0x02, 0x00, 0x00,
                         0x12, 0x34, 0x56}; // Locally administered address
  ret =
      esp_eth_ioctl(s_ethernet_state.eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set MAC address: %s", esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Set MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
           mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  // Test W5500 communication by reading back MAC address
  uint8_t read_mac[6] = {0};
  ret =
      esp_eth_ioctl(s_ethernet_state.eth_handle, ETH_CMD_G_MAC_ADDR, read_mac);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read MAC address from W5500: %s",
             esp_err_to_name(ret));
    ESP_LOGE(TAG, "This indicates SPI communication problem with W5500");
    return ret;
  }
  ESP_LOGI(TAG, "Read back MAC: %02x:%02x:%02x:%02x:%02x:%02x", read_mac[0],
           read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);

  // Verify MAC address was set correctly
  if (memcmp(mac_addr, read_mac, 6) != 0) {
    ESP_LOGE(
        TAG,
        "MAC address verification failed! SPI communication issue with W5500");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "W5500 MAC address verification successful");

  // Save MAC address to state structure
  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  memcpy(s_ethernet_state.mac_addr, read_mac, 6);
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "W5500 hardware initialized successfully");
  return ESP_OK;
}

static esp_err_t ethernet_hw_deinit(void) {
  ESP_LOGI(TAG, "Deinitializing W5500 hardware...");

  if (s_ethernet_state.eth_handle) {
    esp_eth_driver_uninstall(s_ethernet_state.eth_handle);
    s_ethernet_state.eth_handle = NULL;
  }

  // Reset W5500 to power down state
  gpio_set_level(W5500_RST_GPIO, 0);

  // Deinitialize SPI bus
  spi_bus_free(W5500_SPI_HOST);

  ESP_LOGI(TAG, "W5500 hardware deinitialized");
  return ESP_OK;
}

static esp_err_t ethernet_netif_init(void) {
  esp_err_t ret = ESP_OK;

  ESP_LOGI(TAG, "Initializing network interface...");

  // Initialize network interface
  ret = esp_netif_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to create default event loop: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Create ethernet network interface with static IP configuration
  esp_netif_ip_info_t ip_info = {0};
  ip_info.ip.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.ip_addr);
  ip_info.gw.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.gateway);
  ip_info.netmask.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.netmask);

  esp_netif_inherent_config_t eth_behav_cfg = {
      .flags = ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP,
      .ip_info = &ip_info,
      .get_ip_event = IP_EVENT_ETH_GOT_IP,
      .lost_ip_event = IP_EVENT_ETH_LOST_IP,
      .if_key = "ETH_DEF",
      .if_desc = "eth",
      .route_prio = 60,
  };

  esp_netif_config_t eth_cfg = {
      .base = &eth_behav_cfg,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };

  s_ethernet_state.netif = esp_netif_new(&eth_cfg);
  if (!s_ethernet_state.netif) {
    ESP_LOGE(TAG, "Failed to create ethernet netif");
    return ESP_FAIL;
  }

  // Attach ethernet driver to network interface
  ret = esp_netif_attach(s_ethernet_state.netif,
                         esp_eth_new_netif_glue(s_ethernet_state.eth_handle));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to attach netif: %s", esp_err_to_name(ret));
    return ret;
  }

  // Register event handler
  ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                   &ethernet_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register ETH event handler: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                   &ethernet_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register IP event handler: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Configure DHCP server lease range - must be done before DHCP server starts
  // First stop DHCP server if it's already started
  esp_netif_dhcps_stop(s_ethernet_state.netif);

  // Set DHCP lease range
  dhcps_lease_t lease;
  lease.start_ip.addr =
      ip_string_to_uint32(s_ethernet_state.config.dhcp_server.pool_start);
  lease.end_ip.addr =
      ip_string_to_uint32(s_ethernet_state.config.dhcp_server.pool_end);

  ret = esp_netif_dhcps_option(s_ethernet_state.netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_REQUESTED_IP_ADDRESS, &lease,
                               sizeof(lease));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set DHCP lease range: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "DHCP lease range configured: %s - %s",
             s_ethernet_state.config.dhcp_server.pool_start,
             s_ethernet_state.config.dhcp_server.pool_end);
  }

  // Stop DHCP server to reconfigure it
  esp_netif_dhcps_stop(s_ethernet_state.netif);

  // Configure DHCP server to offer DNS server to clients
  uint8_t dhcps_offer_option = OFFER_DNS;
  ret = esp_netif_dhcps_option(s_ethernet_state.netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_DOMAIN_NAME_SERVER,
                               &dhcps_offer_option, sizeof(dhcps_offer_option));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable DHCP DNS offering: %s",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "DHCP DNS offering enabled");
  }

  // Set the actual DNS server address
  esp_netif_dns_info_t dns_info;
  dns_info.ip.u_addr.ip4.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.dns_server);
  dns_info.ip.type = IPADDR_TYPE_V4;
  ret = esp_netif_set_dns_info(s_ethernet_state.netif, ESP_NETIF_DNS_MAIN,
                               &dns_info);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set DNS server address: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "DHCP DNS server configured: %s",
             s_ethernet_state.config.network.dns_server);
  }

  // Restart DHCP server with new configuration
  ret = esp_netif_dhcps_start(s_ethernet_state.netif);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to restart DHCP server: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "DHCP server restarted with DNS configuration");
    // Log detailed DHCP server startup information
    char dhcp_init_msg[128];
    snprintf(
        dhcp_init_msg, sizeof(dhcp_init_msg),
        "DHCP server initialized - Pool: %s-%s, DNS: %s, Lease: %luh",
        s_ethernet_state.config.dhcp_server.pool_start,
        s_ethernet_state.config.dhcp_server.pool_end,
        s_ethernet_state.config.network.dns_server,
        (unsigned long)s_ethernet_state.config.dhcp_server.lease_time_hours);
    ethernet_log_network_activity(dhcp_init_msg);
  }

  // Update DHCP server configuration to reflect actual state
  // Since we created netif with ESP_NETIF_DHCP_SERVER flag, it's actually
  // enabled
  s_ethernet_state.config.dhcp_server.enable = true;

  ESP_LOGI(TAG, "Network interface initialized successfully");
  return ESP_OK;
}

static esp_err_t ethernet_netif_deinit(void) {
  ESP_LOGI(TAG, "Deinitializing network interface...");

  // Unregister event handlers
  esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID,
                               &ethernet_event_handler);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                               &ethernet_event_handler);

  // Destroy network interface
  if (s_ethernet_state.netif) {
    esp_netif_destroy(s_ethernet_state.netif);
    s_ethernet_state.netif = NULL;
  }

  ESP_LOGI(TAG, "Network interface deinitialized");
  return ESP_OK;
}

/* ============================================================================
 * Event Handling
 * ============================================================================
 */

static void ethernet_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data) {
  ESP_LOGD(TAG, "Ethernet event: base=%s, id=%ld", event_base, event_id);

  if (event_base == ETH_EVENT) {
    switch (event_id) {
    case ETHERNET_EVENT_START:
      ESP_LOGI(TAG, "Ethernet Started");
      s_ethernet_state.status = ETHERNET_STATUS_READY;
      ethernet_notify_status_change(ETHERNET_STATUS_READY);
      break;

    case ETHERNET_EVENT_STOP:
      ESP_LOGI(TAG, "Ethernet Stopped");
      s_ethernet_state.status = ETHERNET_STATUS_INITIALIZED;
      s_ethernet_state.link_up = false;
      ethernet_notify_status_change(ETHERNET_STATUS_INITIALIZED);
      break;

    case ETHERNET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Ethernet Link Up");
      // Log detailed connection information
      char detail_msg[128];
      snprintf(detail_msg, sizeof(detail_msg),
               "Ethernet connected - MAC: %02x:%02x:%02x:%02x:%02x:%02x, "
               "Speed: 100Mbps, Full-Duplex",
               s_ethernet_state.mac_addr[0], s_ethernet_state.mac_addr[1],
               s_ethernet_state.mac_addr[2], s_ethernet_state.mac_addr[3],
               s_ethernet_state.mac_addr[4], s_ethernet_state.mac_addr[5]);
      ethernet_log_network_activity(detail_msg);
      s_ethernet_state.link_up = true;
      s_ethernet_state.status = ETHERNET_STATUS_CONNECTED;
      ethernet_notify_status_change(ETHERNET_STATUS_CONNECTED);
      break;

    case ETHERNET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "Ethernet Link Down");
      ethernet_log_network_activity(
          "Ethernet disconnected - Link lost, checking cable connection");
      s_ethernet_state.link_up = false;
      s_ethernet_state.status = ETHERNET_STATUS_READY;
      ethernet_notify_status_change(ETHERNET_STATUS_READY);
      break;

    default:
      ESP_LOGD(TAG, "Unknown ETH event: %ld", event_id);
      break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
    case IP_EVENT_ETH_GOT_IP: {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "Ethernet Got IP Address");
      ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
      ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
      ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));

      s_ethernet_state.status = ETHERNET_STATUS_CONNECTED;
      s_ethernet_state.rx_packets++; // Count IP event as activity
      ethernet_notify_status_change(ETHERNET_STATUS_CONNECTED);
      break;
    }
    case IP_EVENT_ETH_LOST_IP:
      ESP_LOGI(TAG, "Ethernet Lost IP Address");
      s_ethernet_state.status = ETHERNET_STATUS_READY;
      ethernet_notify_status_change(ETHERNET_STATUS_READY);
      break;

    default:
      ESP_LOGD(TAG, "Unknown IP event: %ld", event_id);
      break;
    }
  }
}

static void ethernet_notify_status_change(ethernet_status_t new_status) {
  // Notify event system (robOS integration)
  if (event_manager_is_initialized()) {
    // TODO: Define ethernet event types in event_manager
    ESP_LOGD(TAG, "Notifying status change: %d", new_status);
  }

  // Call user callback if registered
  if (s_ethernet_state.event_callback) {
    s_ethernet_state.event_callback(new_status,
                                    s_ethernet_state.event_user_data);
  }
}

static void ethernet_log_network_activity(const char *activity) {
  if (!activity || !s_ethernet_state.initialized) {
    return;
  }

  // Get current time as timestamp (seconds since boot)
  uint32_t timestamp_sec = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

  // Calculate hours, minutes, seconds
  uint32_t hours = timestamp_sec / 3600;
  uint32_t minutes = (timestamp_sec % 3600) / 60;
  uint32_t seconds = timestamp_sec % 60;

  // Format entry with readable timestamp
  snprintf(s_ethernet_state.activity_log
               .entries[s_ethernet_state.activity_log.current_index],
           sizeof(s_ethernet_state.activity_log.entries[0]),
           "%02lu:%02lu:%02lu - %s", (unsigned long)hours,
           (unsigned long)minutes, (unsigned long)seconds, activity);

  // Update indices
  s_ethernet_state.activity_log.current_index =
      (s_ethernet_state.activity_log.current_index + 1) % 32;
  s_ethernet_state.activity_log.total_entries++;

  // Log at debug level to avoid console noise
  ESP_LOGD(TAG, "Network activity: %s", activity);
}

/**
 * @brief Monitor DHCP client connections through ARP table inspection
 */
static void ethernet_monitor_dhcp_clients(void) {
  if (!s_ethernet_state.initialized ||
      !s_ethernet_state.config.dhcp_server.enable ||
      !s_ethernet_state.link_up) {
    return;
  }

  static uint32_t last_check_time = 0;
  static ip4_addr_t known_clients[10] = {0}; // Track up to 10 clients
  static uint8_t known_client_count = 0;

  uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

  // Check every 10 seconds for new DHCP clients
  if (current_time - last_check_time < 10) {
    return;
  }

  last_check_time = current_time;

  // Check ARP table for devices in our DHCP range (10.10.99.100 - 10.10.99.110)
  ip4_addr_t dhcp_start;
  IP4_ADDR(&dhcp_start, 10, 10, 99, 100);

  for (int i = 0; i <= 10; i++) { // Check .100 to .110
    ip4_addr_t check_ip;
    IP4_ADDR(&check_ip, 10, 10, 99, 100 + i);

    // Try to find this IP in ARP table
    struct netif *netif = esp_netif_get_netif_impl(s_ethernet_state.netif);
    if (netif) {
      struct eth_addr *ethaddr;
      const ip4_addr_t *ipaddr;
      s8_t arp_idx = etharp_find_addr(netif, &check_ip, &ethaddr, &ipaddr);

      if (arp_idx >= 0) {
        // Check if this is a new client
        bool is_new_client = true;
        for (uint8_t j = 0; j < known_client_count; j++) {
          if (ip4_addr_cmp(&known_clients[j], &check_ip)) {
            is_new_client = false;
            break;
          }
        }

        if (is_new_client && known_client_count < 10) {
          // MAC address is already obtained from etharp_find_addr
          char client_msg[128];
          snprintf(client_msg, sizeof(client_msg),
                   "DHCP client connected - IP: " IPSTR
                   ", MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                   IP2STR(&check_ip), ethaddr->addr[0], ethaddr->addr[1],
                   ethaddr->addr[2], ethaddr->addr[3], ethaddr->addr[4],
                   ethaddr->addr[5]);
          ethernet_log_network_activity(client_msg);

          // Add to known clients list
          known_clients[known_client_count] = check_ip;
          known_client_count++;
        }
      }
    }
  }

  // Cleanup disconnected clients from known list
  uint8_t active_clients = 0;
  for (uint8_t i = 0; i < known_client_count; i++) {
    struct netif *netif = esp_netif_get_netif_impl(s_ethernet_state.netif);
    if (netif) {
      struct eth_addr *ethaddr;
      const ip4_addr_t *ipaddr;
      s8_t arp_idx =
          etharp_find_addr(netif, &known_clients[i], &ethaddr, &ipaddr);
      if (arp_idx >= 0) {
        // Client still active, keep it
        if (active_clients != i) {
          known_clients[active_clients] = known_clients[i];
        }
        active_clients++;
      } else {
        // Client disconnected
        char disconnect_msg[128];
        snprintf(disconnect_msg, sizeof(disconnect_msg),
                 "DHCP client disconnected - IP: " IPSTR,
                 IP2STR(&known_clients[i]));
        ethernet_log_network_activity(disconnect_msg);
      }
    }
  }
  known_client_count = active_clients;
}

/* ============================================================================
 * Configuration Storage Integration
 * ============================================================================
 */

static esp_err_t ethernet_load_config_from_storage(void) {
  if (!config_manager_is_initialized()) {
    ESP_LOGW(TAG, "Config manager not initialized, using defaults");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGD(TAG, "Loading ethernet configuration from storage");

  // Load network configuration
  size_t str_len = sizeof(s_ethernet_state.config.network.ip_addr);
  esp_err_t ret =
      config_manager_get("ethernet", "ip_addr", CONFIG_TYPE_STRING,
                         s_ethernet_state.config.network.ip_addr, &str_len);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Loaded IP address: %s",
             s_ethernet_state.config.network.ip_addr);
  }

  str_len = sizeof(s_ethernet_state.config.network.netmask);
  ret = config_manager_get("ethernet", "netmask", CONFIG_TYPE_STRING,
                           s_ethernet_state.config.network.netmask, &str_len);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Loaded netmask: %s",
             s_ethernet_state.config.network.netmask);
  }

  str_len = sizeof(s_ethernet_state.config.network.gateway);
  ret = config_manager_get("ethernet", "gateway", CONFIG_TYPE_STRING,
                           s_ethernet_state.config.network.gateway, &str_len);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Loaded gateway: %s",
             s_ethernet_state.config.network.gateway);
  }

  str_len = sizeof(s_ethernet_state.config.network.dns_server);
  ret =
      config_manager_get("ethernet", "dns_server", CONFIG_TYPE_STRING,
                         s_ethernet_state.config.network.dns_server, &str_len);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Loaded DNS server: %s",
             s_ethernet_state.config.network.dns_server);
  }

  return ESP_OK;
}

static esp_err_t ethernet_save_config_to_storage(void) {
  if (!config_manager_is_initialized()) {
    ESP_LOGW(TAG, "Config manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Saving ethernet configuration to NVS");

  esp_err_t ret = ESP_OK;

  // Save network configuration
  ret |=
      config_manager_set("ethernet", "ip_addr", CONFIG_TYPE_STRING,
                         s_ethernet_state.config.network.ip_addr,
                         strlen(s_ethernet_state.config.network.ip_addr) + 1);
  ret |=
      config_manager_set("ethernet", "netmask", CONFIG_TYPE_STRING,
                         s_ethernet_state.config.network.netmask,
                         strlen(s_ethernet_state.config.network.netmask) + 1);
  ret |=
      config_manager_set("ethernet", "gateway", CONFIG_TYPE_STRING,
                         s_ethernet_state.config.network.gateway,
                         strlen(s_ethernet_state.config.network.gateway) + 1);
  ret |= config_manager_set("ethernet", "dns_server", CONFIG_TYPE_STRING,
                            s_ethernet_state.config.network.dns_server,
                            strlen(s_ethernet_state.config.network.dns_server) +
                                1);

  if (ret == ESP_OK) {
    ret = config_manager_commit();
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Ethernet configuration saved successfully");
    } else {
      ESP_LOGE(TAG, "Failed to commit ethernet configuration: %s",
               esp_err_to_name(ret));
    }
  } else {
    ESP_LOGE(TAG, "Failed to save ethernet configuration parameters");
  }

  return ret;
}

/* ============================================================================
 * Configuration Functions (Basic Implementation)
 * ============================================================================
 */

esp_err_t
ethernet_manager_set_network_config(const ethernet_network_config_t *config) {
  if (!config || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  memcpy(&s_ethernet_state.config.network, config,
         sizeof(ethernet_network_config_t));
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "Network configuration updated");
  return ESP_OK;
}

esp_err_t
ethernet_manager_get_network_config(ethernet_network_config_t *config) {
  if (!config || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  memcpy(config, &s_ethernet_state.config.network,
         sizeof(ethernet_network_config_t));
  xSemaphoreGive(s_ethernet_state.mutex);

  return ESP_OK;
}

esp_err_t
ethernet_manager_set_dhcp_config(const ethernet_dhcp_config_t *config) {
  if (!config || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  memcpy(&s_ethernet_state.config.dhcp_server, config,
         sizeof(ethernet_dhcp_config_t));
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP server configuration updated");
  return ESP_OK;
}

esp_err_t ethernet_manager_get_dhcp_config(ethernet_dhcp_config_t *config) {
  if (!config || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  memcpy(config, &s_ethernet_state.config.dhcp_server,
         sizeof(ethernet_dhcp_config_t));
  xSemaphoreGive(s_ethernet_state.mutex);

  return ESP_OK;
}

/* ============================================================================
 * Network Control Functions (Stub Implementation)
 * ============================================================================
 */

esp_err_t ethernet_manager_reset(void) {
  if (!s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Applying network configuration changes...");

  // Stop if running to safely update configuration
  bool was_started = s_ethernet_state.started;
  if (was_started) {
    ethernet_manager_stop();
  }

  // Stop DHCP client multiple times to ensure it's fully stopped
  ESP_LOGI(TAG, "Ensuring DHCP client is stopped...");
  esp_err_t ret;
  for (int i = 0; i < 3; i++) {
    ret = esp_netif_dhcpc_stop(s_ethernet_state.netif);
    if (ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
      ESP_LOGI(TAG, "DHCP client confirmed stopped");
      break;
    } else if (ret == ESP_OK) {
      ESP_LOGI(TAG, "DHCP client stopped successfully");
      vTaskDelay(pdMS_TO_TICKS(100)); // Wait for stop to complete
      break;
    } else {
      ESP_LOGW(TAG, "DHCP client stop attempt %d failed: %s", i + 1,
               esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }

  // Apply new IP configuration to the network interface
  ESP_LOGI(TAG, "Updating IP configuration...");
  esp_netif_ip_info_t ip_info = {0};
  ip_info.ip.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.ip_addr);
  ip_info.gw.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.gateway);
  ip_info.netmask.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.netmask);

  ret = esp_netif_set_ip_info(s_ethernet_state.netif, &ip_info);
  if (ret != ESP_OK) {
    ESP_LOGW(
        TAG,
        "Software IP update failed: %s, trying network interface restart...",
        esp_err_to_name(ret));

    // Force network interface down and up to apply new configuration
    ESP_LOGI(TAG, "Performing network interface restart...");
    if (was_started) {
      // Interface was already stopped, now start it again with new config
      ret = ethernet_manager_start();
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restart ethernet after IP update failure");
      } else {
        ESP_LOGI(TAG, "Network interface restarted with new configuration");

        // After restart, wait for interface to be ready and stop DHCP client
        // again
        vTaskDelay(
            pdMS_TO_TICKS(300)); // Wait longer for interface to be fully ready

        // Stop DHCP client again after restart with more aggressive approach
        ESP_LOGI(TAG, "Stopping DHCP client after network restart...");
        for (int i = 0; i < 8; i++) { // Even more attempts after restart
          ret = esp_netif_dhcpc_stop(s_ethernet_state.netif);
          if (ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGI(TAG, "DHCP client confirmed stopped after restart");
            vTaskDelay(pdMS_TO_TICKS(50)); // Give time for state to settle
            break;
          } else if (ret == ESP_OK) {
            ESP_LOGI(TAG, "DHCP client stopped successfully after restart");
            vTaskDelay(pdMS_TO_TICKS(150)); // Longer wait for stop to complete
            break;
          } else {
            ESP_LOGW(TAG,
                     "DHCP client stop attempt %d after restart failed: %s",
                     i + 1, esp_err_to_name(ret));
            vTaskDelay(
                pdMS_TO_TICKS(150)); // Much longer delay between attempts
          }
        }

        // Force a brief interface down/up cycle to reset internal state
        ESP_LOGI(TAG, "Forcing interface state reset...");
        esp_netif_action_stop(s_ethernet_state.netif, NULL, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_netif_action_start(s_ethernet_state.netif, NULL, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(200));

        // Try to stop DHCP client one more time after interface reset
        ret = esp_netif_dhcpc_stop(s_ethernet_state.netif);
        ESP_LOGI(TAG, "Final DHCP client stop result: %s",
                 esp_err_to_name(ret));

        // Now try to apply IP configuration again
        ret = esp_netif_set_ip_info(s_ethernet_state.netif, &ip_info);
        if (ret == ESP_OK) {
          ESP_LOGI(TAG,
                   "IP configuration applied after restart - IP: %s, Gateway: "
                   "%s, Netmask: %s",
                   s_ethernet_state.config.network.ip_addr,
                   s_ethernet_state.config.network.gateway,
                   s_ethernet_state.config.network.netmask);
        } else {
          ESP_LOGI(TAG, "Dynamic IP configuration requires system restart to "
                        "take full effect");
          ESP_LOGD(TAG, "IP configuration error details: %s",
                   esp_err_to_name(ret));
        }
      }
    }
  } else {
    ESP_LOGI(TAG, "IP configuration applied - IP: %s, Gateway: %s, Netmask: %s",
             s_ethernet_state.config.network.ip_addr,
             s_ethernet_state.config.network.gateway,
             s_ethernet_state.config.network.netmask);
  }

  // Update DNS configuration
  ESP_LOGI(TAG, "Updating DNS configuration...");
  esp_netif_dns_info_t dns_info;
  dns_info.ip.u_addr.ip4.addr =
      ip_string_to_uint32(s_ethernet_state.config.network.dns_server);
  dns_info.ip.type = IPADDR_TYPE_V4;
  ret = esp_netif_set_dns_info(s_ethernet_state.netif, ESP_NETIF_DNS_MAIN,
                               &dns_info);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set DNS server: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "DNS server updated to: %s",
             s_ethernet_state.config.network.dns_server);
  }

  // Perform a quick network refresh to ensure new IP is active
  ESP_LOGI(TAG, "Refreshing network interface to activate new IP...");
  gpio_set_level(W5500_RST_GPIO, 0);
  vTaskDelay(pdMS_TO_TICKS(5)); // Very short reset pulse
  gpio_set_level(W5500_RST_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(50)); // Brief recovery time

  // Log the configuration change
  char config_msg[128];
  snprintf(config_msg, sizeof(config_msg),
           "Network configuration updated - IP: %s, Gateway: %s",
           s_ethernet_state.config.network.ip_addr,
           s_ethernet_state.config.network.gateway);
  ethernet_log_network_activity(config_msg);

  // Restart if it was running
  if (was_started) {
    ret = ethernet_manager_start();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to restart after configuration update: %s",
               esp_err_to_name(ret));
      return ret;
    }
  }

  ESP_LOGI(TAG, "Network configuration changes applied successfully");
  return ESP_OK;
}

bool ethernet_manager_is_link_up(void) { return s_ethernet_state.link_up; }

esp_err_t ethernet_manager_get_mac_address(uint8_t *mac_addr) {
  if (!mac_addr || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_ethernet_state.eth_handle) {
    ESP_LOGE(TAG, "Ethernet handle not available");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret =
      esp_eth_ioctl(s_ethernet_state.eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
    return ret;
  }

  return ESP_OK;
}

/* ============================================================================
 * Event System Integration
 * ============================================================================
 */

esp_err_t
ethernet_manager_register_event_callback(ethernet_event_callback_t callback,
                                         void *user_data) {
  if (!callback) {
    return ESP_ERR_INVALID_ARG;
  }

  s_ethernet_state.event_callback = callback;
  s_ethernet_state.event_user_data = user_data;

  ESP_LOGI(TAG, "Event callback registered");
  return ESP_OK;
}

/* ============================================================================
 * Console Integration
 * ============================================================================
 */

esp_err_t ethernet_manager_register_console_commands(void) {
  return ethernet_console_init();
}

esp_err_t ethernet_manager_unregister_console_commands(void) {
  return ethernet_console_deinit();
}

/* ============================================================================
 * DHCP Server Management
 * ============================================================================
 */

esp_err_t ethernet_manager_dhcp_server_start(void) {
  if (!s_ethernet_state.initialized) {
    ESP_LOGE(TAG, "Ethernet manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_ethernet_state.netif) {
    ESP_LOGE(TAG, "Network interface not available");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Starting DHCP server...");

  // DHCP server should already be enabled due to ESP_NETIF_DHCP_SERVER flag
  // but we'll ensure it's started
  esp_err_t ret = esp_netif_dhcps_start(s_ethernet_state.netif);
  if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
    ESP_LOGE(TAG, "Failed to start DHCP server: %s", esp_err_to_name(ret));
    return ret;
  }

  // Update configuration state
  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  s_ethernet_state.config.dhcp_server.enable = true;
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP server started successfully");
  // Log detailed DHCP server information
  char dhcp_msg[128];
  snprintf(dhcp_msg, sizeof(dhcp_msg),
           "DHCP server started - Pool: %s-%s, DNS: %s, Lease: %luh",
           s_ethernet_state.config.dhcp_server.pool_start,
           s_ethernet_state.config.dhcp_server.pool_end,
           s_ethernet_state.config.network.dns_server,
           (unsigned long)s_ethernet_state.config.dhcp_server.lease_time_hours);
  ethernet_log_network_activity(dhcp_msg);
  return ESP_OK;
}

esp_err_t ethernet_manager_dhcp_server_stop(void) {
  if (!s_ethernet_state.initialized) {
    ESP_LOGE(TAG, "Ethernet manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_ethernet_state.netif) {
    ESP_LOGE(TAG, "Network interface not available");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Stopping DHCP server...");

  esp_err_t ret = esp_netif_dhcps_stop(s_ethernet_state.netif);
  if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    ESP_LOGE(TAG, "Failed to stop DHCP server: %s", esp_err_to_name(ret));
    return ret;
  }

  // Update configuration state
  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  s_ethernet_state.config.dhcp_server.enable = false;
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP server stopped successfully");
  ethernet_log_network_activity(
      "DHCP server stopped - No longer serving IP addresses");
  return ESP_OK;
}

uint32_t ethernet_manager_get_activity_log(char entries[][128],
                                           uint32_t max_entries,
                                           uint32_t *total_entries) {
  if (!entries || !total_entries || !s_ethernet_state.initialized) {
    return 0;
  }

  *total_entries = s_ethernet_state.activity_log.total_entries;

  uint32_t entries_to_copy = (max_entries < 32) ? max_entries : 32;
  uint32_t available_entries =
      (s_ethernet_state.activity_log.total_entries < 32)
          ? s_ethernet_state.activity_log.total_entries
          : 32;

  if (entries_to_copy > available_entries) {
    entries_to_copy = available_entries;
  }

  // Copy entries in reverse chronological order (newest first)
  uint32_t copied = 0;
  int start_index = (s_ethernet_state.activity_log.current_index - 1 + 32) % 32;

  for (uint32_t i = 0; i < entries_to_copy && copied < max_entries; i++) {
    int index = (start_index - i + 32) % 32;

    // Only copy non-empty entries
    if (s_ethernet_state.activity_log.entries[index][0] != '\0') {
      strncpy(entries[copied], s_ethernet_state.activity_log.entries[index],
              127);
      entries[copied][127] = '\0';
      copied++;
    }
  }

  return copied;
}

/**
 * @brief DHCP monitoring task
 */
static void ethernet_dhcp_monitor_task(void *params) {
  (void)params;

  ESP_LOGI(TAG, "DHCP monitor task started");

  while (1) {
    ethernet_monitor_dhcp_clients();
    vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
  }
}

/* ============================================================================
 * Individual Parameter Configuration Functions
 * ============================================================================
 */

esp_err_t ethernet_manager_set_ip_address(const char *ip_addr) {
  if (!ip_addr || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  strncpy(s_ethernet_state.config.network.ip_addr, ip_addr,
          sizeof(s_ethernet_state.config.network.ip_addr) - 1);
  s_ethernet_state.config.network
      .ip_addr[sizeof(s_ethernet_state.config.network.ip_addr) - 1] = '\0';
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "IP address set to: %s", ip_addr);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_netmask(const char *netmask) {
  if (!netmask || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  strncpy(s_ethernet_state.config.network.netmask, netmask,
          sizeof(s_ethernet_state.config.network.netmask) - 1);
  s_ethernet_state.config.network
      .netmask[sizeof(s_ethernet_state.config.network.netmask) - 1] = '\0';
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "Netmask set to: %s", netmask);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_gateway(const char *gateway) {
  if (!gateway || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  strncpy(s_ethernet_state.config.network.gateway, gateway,
          sizeof(s_ethernet_state.config.network.gateway) - 1);
  s_ethernet_state.config.network
      .gateway[sizeof(s_ethernet_state.config.network.gateway) - 1] = '\0';
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "Gateway set to: %s", gateway);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_dns_server(const char *dns_server) {
  if (!dns_server || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  strncpy(s_ethernet_state.config.network.dns_server, dns_server,
          sizeof(s_ethernet_state.config.network.dns_server) - 1);
  s_ethernet_state.config.network
      .dns_server[sizeof(s_ethernet_state.config.network.dns_server) - 1] =
      '\0';
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DNS server set to: %s", dns_server);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_dhcp_pool_start(const char *pool_start) {
  if (!pool_start || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  strncpy(s_ethernet_state.config.dhcp_server.pool_start, pool_start,
          sizeof(s_ethernet_state.config.dhcp_server.pool_start) - 1);
  s_ethernet_state.config.dhcp_server
      .pool_start[sizeof(s_ethernet_state.config.dhcp_server.pool_start) - 1] =
      '\0';
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP pool start set to: %s", pool_start);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_dhcp_pool_end(const char *pool_end) {
  if (!pool_end || !s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  strncpy(s_ethernet_state.config.dhcp_server.pool_end, pool_end,
          sizeof(s_ethernet_state.config.dhcp_server.pool_end) - 1);
  s_ethernet_state.config.dhcp_server
      .pool_end[sizeof(s_ethernet_state.config.dhcp_server.pool_end) - 1] =
      '\0';
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP pool end set to: %s", pool_end);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_dhcp_lease_time(uint32_t hours) {
  if (!s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  s_ethernet_state.config.dhcp_server.lease_time_hours = hours;
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP lease time set to: %lu hours", (unsigned long)hours);
  return ESP_OK;
}

esp_err_t ethernet_manager_set_dhcp_max_clients(uint8_t max_clients) {
  if (!s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  s_ethernet_state.config.dhcp_server.max_clients = max_clients;
  xSemaphoreGive(s_ethernet_state.mutex);

  ESP_LOGI(TAG, "DHCP max clients set to: %u", max_clients);
  return ESP_OK;
}

esp_err_t ethernet_manager_save_config(void) {
  if (!s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Saving ethernet configuration to NVS");
  return ethernet_save_config_to_storage();
}

esp_err_t ethernet_manager_load_config(void) {
  if (!s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Loading ethernet configuration from NVS");
  return ethernet_load_config_from_storage();
}

esp_err_t ethernet_manager_reset_config(void) {
  if (!s_ethernet_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Resetting ethernet configuration to defaults");

  // Get default configuration
  ethernet_manager_config_t default_config = ETHERNET_DEFAULT_CONFIG();

  xSemaphoreTake(s_ethernet_state.mutex, portMAX_DELAY);
  memcpy(&s_ethernet_state.config, &default_config,
         sizeof(ethernet_manager_config_t));
  xSemaphoreGive(s_ethernet_state.mutex);

  // Save the default configuration to NVS
  esp_err_t ret = ethernet_save_config_to_storage();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save default configuration: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Configuration reset to defaults and saved");
  return ESP_OK;
}