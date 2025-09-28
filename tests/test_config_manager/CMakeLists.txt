cmake_minimum_required(VERSION 3.5)

# Include the main ESP-IDF CMake project
set(EXTRA_COMPONENT_DIRS 
    "../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_config_manager)