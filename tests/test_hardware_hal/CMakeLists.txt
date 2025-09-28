cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../../components")
set(COMPONENTS "main" "hardware_hal")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_hardware_hal)