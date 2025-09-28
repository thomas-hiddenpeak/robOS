idf_component_register(
    SRCS "test_console_core.c"
    INCLUDE_DIRS "."
    REQUIRES "unity" "console_core" "event_manager" "hardware_hal"
)