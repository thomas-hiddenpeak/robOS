idf_component_register(
    SRCS "gpio_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common freertos
)
