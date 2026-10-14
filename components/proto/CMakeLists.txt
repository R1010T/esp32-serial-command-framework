# ESP-IDF component: the protocol code, built into the ESP32 firmware.
# The laptop unit tests build the same .c files through test/CMakeLists.txt.
idf_component_register(
    SRCS "crc16.c" "frame.c" "parser.c" "dispatch.c"
    INCLUDE_DIRS "include"
)
