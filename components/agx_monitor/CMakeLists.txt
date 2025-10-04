idf_component_register(SRCS "agx_monitor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hardware_hal console_core driver freertos config_manager event_manager json nvs_flash esp_timer)