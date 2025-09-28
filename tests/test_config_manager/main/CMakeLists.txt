idf_component_register(
    SRCS "test_config_manager_simple.c"
    INCLUDE_DIRS "."
    REQUIRES 
        unity
        nvs_flash 
        freertos
        config_manager
)