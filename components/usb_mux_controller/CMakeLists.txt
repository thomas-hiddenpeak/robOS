idf_component_register(
    SRCS "usb_mux_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common freertos gpio_controller
)