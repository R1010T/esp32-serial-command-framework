cmake_minimum_required(VERSION 3.16)

# Host unit tests: built with the laptop's gcc (MinGW on Windows),
# not ESP-IDF's compiler. Same pin as Project 1 (ESP-IDF's clang
# on PATH can't link Windows programs).
set(CMAKE_C_COMPILER gcc)

project(proto_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The protocol code lives where the ESP32 build will also find it (Milestone 2).
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/proto)

# Unity test framework (git submodule in test/unity)
add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)

# Code under test: the exact files that will run on the ESP32
add_library(proto STATIC
    ${PROTO_DIR}/crc16.c
    ${PROTO_DIR}/frame.c
    ${PROTO_DIR}/parser.c
)
target_include_directories(proto PUBLIC ${PROTO_DIR}/include)
target_compile_options(proto PRIVATE -Wall -Wextra -Werror)

enable_testing()

foreach(t test_crc16 test_frame test_parser)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} proto unity)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
