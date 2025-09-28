idf_component_register(SRCS "config_commands.c" "config_manager.c" "test_config_commands.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash freertos console
                       PRIV_REQUIRES console_core)