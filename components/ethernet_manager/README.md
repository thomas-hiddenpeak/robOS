# Ethernet Manager Component

## Overview

The Ethernet Manager component provides comprehensive ethernet networking functionality for the robOS system. It manages W5500 ethernet hardware, network configuration, DHCP server capabilities, and integrates seamlessly with robOS's modular architecture.

## Features

- **W5500 Hardware Support**: Full W5500 ethernet controller support via SPI interface
- **Network Configuration**: Static IP and DHCP client modes
- **DHCP Server**: Built-in DHCP server with client management
- **Console Commands**: Rich command-line interface for network management
- **Configuration Persistence**: Integration with robOS config_manager
- **Event Integration**: Seamless integration with robOS event_manager
- **Thread Safety**: Full concurrent access protection

## Hardware Configuration

### W5500 Ethernet Controller (SPI2_HOST)
- **RST**: GPIO 39 - Reset signal
- **INT**: GPIO 38 - Interrupt signal  
- **MISO**: GPIO 13 - SPI data input
- **SCLK**: GPIO 12 - SPI clock
- **MOSI**: GPIO 11 - SPI data output
- **CS**: GPIO 10 - SPI chip select

### Default Network Configuration
- **IP Address**: 10.10.99.97
- **Gateway**: 10.10.99.100
- **Subnet Mask**: 255.255.255.0
- **DNS Server**: 8.8.8.8

### DHCP Server Configuration
- **IP Pool**: 10.10.99.100 - 10.10.99.110
- **Lease Time**: 24 hours
- **Max Clients**: 10

## API Reference

### Core Functions

```c
// Initialize ethernet manager
esp_err_t ethernet_manager_init(const ethernet_manager_config_t *config);

// Start/stop ethernet manager
esp_err_t ethernet_manager_start(void);
esp_err_t ethernet_manager_stop(void);

// Get status information
esp_err_t ethernet_manager_get_status(ethernet_manager_status_t *status);

// Configuration management
esp_err_t ethernet_manager_set_network_config(const ethernet_network_config_t *config);
esp_err_t ethernet_manager_get_network_config(ethernet_network_config_t *config);
```

### Console Commands

#### Basic Status and Control
```bash
eth-status              # Display complete ethernet status
eth-reset               # Reset ethernet interface
```

#### Network Configuration
```bash
eth-config                          # Show current configuration
eth-config set ip <ip_address>      # Set IP address
eth-config set gateway <gateway>    # Set gateway address
eth-config set netmask <netmask>    # Set subnet mask
eth-config set dns <dns_server>     # Set DNS server
eth-config save                     # Save configuration to storage
eth-config load                     # Load configuration from storage
eth-config reset                    # Reset to default configuration
```

## Usage Examples

### Basic Initialization
```c
#include "ethernet_manager.h"

// Initialize with default configuration
esp_err_t ret = ethernet_manager_init(NULL);
if (ret == ESP_OK) {
    // Start ethernet manager
    ethernet_manager_start();
    
    // Register console commands
    ethernet_manager_register_console_commands();
}
```

### Custom Configuration
```c
ethernet_manager_config_t config = ETHERNET_DEFAULT_CONFIG();
strcpy(config.network.ip_addr, "192.168.1.100");
strcpy(config.network.gateway, "192.168.1.1");
config.dhcp_server.enable = true;

esp_err_t ret = ethernet_manager_init(&config);
```

### Event Handling
```c
void ethernet_event_handler(ethernet_status_t status, void *user_data) {
    printf("Ethernet status changed: %d\n", status);
}

ethernet_manager_register_event_callback(ethernet_event_handler, NULL);
```

## Integration with robOS

### Dependencies
- **hardware_hal**: GPIO and SPI hardware abstraction
- **console_core**: Command line interface
- **config_manager**: Configuration persistence
- **event_manager**: Event notification system
- **ESP-IDF**: esp_eth, esp_netif, lwip components

### Configuration Storage
The component integrates with `config_manager` to persist network settings:
- Namespace: "ethernet"
- Keys: "network_config", "dhcp_config"

### Event System
Ethernet events are propagated through `event_manager`:
- Link up/down events
- IP address assignment
- DHCP client connections

## Development Status

### ✅ Implemented Features
- Basic component framework
- Configuration data structures
- Console command interface
- Thread-safe state management
- Integration points for robOS architecture

### 🚧 In Development
- W5500 hardware initialization
- Network interface setup
- DHCP server implementation
- Configuration persistence
- Event system integration

### 📋 Planned Features
- Network gateway functionality
- Advanced DHCP features (MAC reservations)
- Network diagnostics (ping, statistics)
- Web interface integration

## Testing

### Build Test
```bash
cd /path/to/robOS
./flash.sh  # Build and flash
```

### Console Testing
```bash
# After flashing, connect to console
./monitor.sh

# Test commands
eth-status
eth-config
eth-reset
```

## Troubleshooting

### Common Issues
1. **Build Errors**: Ensure all dependencies are properly configured
2. **Hardware Issues**: Verify W5500 SPI connections
3. **Network Issues**: Check cable connections and network configuration

### Debug Logging
Enable debug logging in `sdkconfig`:
```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

## Contributing

When contributing to this component:
1. Follow robOS coding standards
2. Add comprehensive tests for new features
3. Update documentation for API changes
4. Test integration with other robOS components

## License

This component is part of the robOS project and follows the same licensing terms.