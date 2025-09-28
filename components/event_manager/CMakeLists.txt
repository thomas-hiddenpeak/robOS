idf_component_register(SRCS "event_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event freertos esp_timer)