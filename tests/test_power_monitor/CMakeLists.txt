idf_component_register(SRCS "test_power_monitor.c"
                       INCLUDE_DIRS "."
                       REQUIRES unity power_monitor hardware_hal
                       WHOLE_ARCHIVE)