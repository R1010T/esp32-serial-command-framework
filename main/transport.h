/*
 * transport.h - The wire: move bytes in and out of the ESP32
 *
 * The protocol code (components/proto) never touches hardware. This
 * small interface is the only place that does, so switching the wire
 * (native USB today, UART later) means swapping one .c file.
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Start the wire. Call once at boot. */
esp_err_t transport_init(void);

/* Read up to max bytes. Waits at most timeout_ms. Returns the count read (0 = none). */
int transport_read(uint8_t *buf, size_t max, uint32_t timeout_ms);

/* Send len bytes. Waits at most timeout_ms for room. Returns the count sent. */
int transport_write(const uint8_t *buf, size_t len, uint32_t timeout_ms);

#endif /* TRANSPORT_H */
