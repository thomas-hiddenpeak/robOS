idf_component_register(SRCS "matrix_led.c" "test_matrix_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hardware_hal console_core driver freertos config_manager event_manager espressif__led_strip json fatfs)