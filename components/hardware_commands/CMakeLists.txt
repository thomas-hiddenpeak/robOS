idf_component_register(
    SRCS "hardware_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common freertos console_core gpio_controller usb_mux_controller device_controller
)