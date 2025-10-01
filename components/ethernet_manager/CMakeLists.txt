idf_component_register(SRCS "ethernet_manager.c" "ethernet_console.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hardware_hal console_core driver freertos config_manager event_manager esp_eth esp_netif lwip nvs_flash)