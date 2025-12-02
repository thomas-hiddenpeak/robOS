idf_component_register(SRCS "voltage_protection.c"
                       INCLUDE_DIRS "include"
                       REQUIRES power_monitor device_controller agx_monitor touch_led board_led matrix_led fan_controller gpio_controller console_core
                       PRIV_REQUIRES esp_timer freertos)
