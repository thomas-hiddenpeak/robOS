cmake_minimum_required(VERSION 3.16)

# Set target to ESP32S3
set(IDF_TARGET "esp32s3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_console_core)