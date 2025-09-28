idf_component_register(
    SRCS "board_led.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        "led_strip"
        "console_core" 
        "config_manager"
        "driver"
        "esp_timer"
        "freertos"
    PRIV_REQUIRES
        "esp_common"
)