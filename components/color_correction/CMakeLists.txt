idf_component_register(
    SRCS "color_correction.c" "color_console.c"
    INCLUDE_DIRS "."
    REQUIRES "esp_common" "log" "config_manager" "console_core" "json"
)