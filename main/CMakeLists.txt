idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES event_manager hardware_hal console_core fan_controller touch_led board_led
                       PRIV_REQUIRES nvs_flash esp_event)