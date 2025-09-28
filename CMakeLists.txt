cmake_minimum_required(VERSION 3.16)

# Set project name and version
set(PROJECT_VER "1.0.0-dev")
set(PROJECT_NAME "robOS")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})