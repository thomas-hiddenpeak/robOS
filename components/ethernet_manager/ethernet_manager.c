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
#include "esp_system.h"
#include "ethernet_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
      s_ethernet_state.link_up = true;
      s_ethernet_state.status = ETHERNET_STATUS_CONNECTED;
      ethernet_notify_status_change(ETHERNET_STATUS_CONNECTED);
      break;

    case ETHERNET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "Ethernet Link Down");
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

/* ============================================================================
 * Configuration Storage Integration
 * ============================================================================
 */

static esp_err_t ethernet_load_config_from_storage(void) {
  if (!config_manager_is_initialized()) {
    ESP_LOGW(TAG, "Config manager not initialized, using defaults");
    return ESP_ERR_INVALID_STATE;
  }

  // TODO: Implement configuration loading from config_manager
  ESP_LOGD(TAG, "Loading ethernet configuration from storage");

  return ESP_ERR_NOT_FOUND; // Not implemented yet
}

static esp_err_t ethernet_save_config_to_storage(void) {
  if (!config_manager_is_initialized()) {
    ESP_LOGW(TAG, "Config manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // TODO: Implement configuration saving to config_manager
  ESP_LOGD(TAG, "Saving ethernet configuration to storage");

  return ESP_OK;
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

  ESP_LOGI(TAG, "Resetting ethernet interface...");

  // Stop if running
  bool was_started = s_ethernet_state.started;
  if (was_started) {
    ethernet_manager_stop();
  }

  // Hardware reset
  ESP_LOGI(TAG, "Performing hardware reset...");
  gpio_set_level(W5500_RST_GPIO, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(W5500_RST_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(100));

  // Restart if it was running
  if (was_started) {
    esp_err_t ret = ethernet_manager_start();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to restart after reset: %s", esp_err_to_name(ret));
      return ret;
    }
  }

  ESP_LOGI(TAG, "Ethernet interface reset complete");
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
  return ESP_OK;
}