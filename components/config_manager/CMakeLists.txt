idf_component_register(SRCS "base64.c" "config_commands.c" "config_manager.c" "test_config_commands.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash freertos console json
                       PRIV_REQUIRES console_core storage_manager)