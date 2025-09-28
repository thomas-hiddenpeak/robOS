idf_component_register(
    SRCS "touch_led.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        driver
        esp_timer
        freertos
        log
        led_strip
        console
        console_core
        config_manager
        board_led
    PRIV_REQUIRES
        esp_adc
        soc
)