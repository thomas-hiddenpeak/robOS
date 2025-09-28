idf_component_register(
    SRCS "console_core.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "esp_common" "freertos"
    PRIV_REQUIRES "hardware_hal" "event_manager"
)