idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES event_manager hardware_hal console_core fan_controller touch_led board_led storage_manager matrix_led ethernet_manager power_monitor gpio_controller usb_mux_controller device_controller hardware_commands agx_monitor web_server
                       PRIV_REQUIRES nvs_flash esp_event)