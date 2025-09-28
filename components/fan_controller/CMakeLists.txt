idf_component_register(SRCS "fan_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hardware_hal console_core driver freertos config_manager)