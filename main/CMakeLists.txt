idf_component_register(
    SRCS "main.c" "transport_usbjtag.c"
    INCLUDE_DIRS "."
    REQUIRES proto esp_timer driver
)
