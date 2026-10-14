/*
 * transport_usbjtag.c - The wire, over the ESP32-S3's native USB port
 *
 * WHAT:    Moves bytes between the laptop and the firmware through the
 *          chip's built-in USB serial port (USB-Serial-JTAG). On the
 *          laptop it shows up as COM4, the same port used for flashing.
 * WHY:     One cable does everything. The driver keeps a receive buffer
 *          and a send buffer, so no byte is lost while the protocol task
 *          is busy, and reads can wait with a timeout instead of spinning.
 * ANALOGY: A mail slot with a box behind it: letters pile up in the box
 *          until someone collects them.
 *
 * Honest note: this is USB, not a UART. A UART version (second USB-C
 * port, COM5) is a new transport_uart.c with these same 3 functions.
 *
 * INTERVIEW LINE:
 *   "The protocol layer is hardware-independent; one small transport file
 *    wraps the USB-Serial-JTAG driver, with buffered reads and timeouts."
 */
#include "transport.h"

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"

#define RX_BUFFER_SIZE 1024   /* bytes the driver holds until we read them */
#define TX_BUFFER_SIZE 1024   /* bytes the driver holds until USB sends them */

esp_err_t transport_init(void)
{
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = RX_BUFFER_SIZE,
        .tx_buffer_size = TX_BUFFER_SIZE,
    };
    return usb_serial_jtag_driver_install(&cfg);
}

int transport_read(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    return usb_serial_jtag_read_bytes(buf, (uint32_t)max, pdMS_TO_TICKS(timeout_ms));
}

int transport_write(const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(timeout_ms));
}
