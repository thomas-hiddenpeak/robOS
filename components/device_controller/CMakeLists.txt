idf_component_register(
    SRCS "device_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common freertos gpio_controller usb_mux_controller config_manager
)