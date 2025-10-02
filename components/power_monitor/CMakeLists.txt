idf_component_register(SRCS "power_monitor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hardware_hal console_core driver freertos config_manager event_manager esp_adc)