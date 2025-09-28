cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the components to include the tests and the component being tested
set(EXTRA_COMPONENT_DIRS "../../components")

project(test_event_manager)