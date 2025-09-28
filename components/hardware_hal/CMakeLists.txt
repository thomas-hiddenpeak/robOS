idf_component_register(
    SRCS "hardware_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "esp_driver_gpio" "esp_driver_uart" "esp_driver_spi" "esp_driver_ledc" "esp_adc"
)