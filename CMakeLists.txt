# ESP-IDF project: the ESP32 firmware.
# Build from this folder in the ESP-IDF terminal:  idf.py build
# (The laptop unit tests are a separate build: test/CMakeLists.txt.)
cmake_minimum_required(VERSION 3.16)

# Build only what main needs (and what that needs): faster builds.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(serial_cmd_framework)
