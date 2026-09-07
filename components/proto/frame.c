/*
 * frame.c - Packet builder
 *
 * WHAT:    Takes a Type, a Seq and some data, and writes one complete,
 *          ready-to-send packet: sync, header, payload, CRC.
 * WHY:     Every sender (the laptop tool, the ESP32's replies) builds
 *          packets the same way, so the receiver never gets a malformed
 *          one by mistake.
 * ANALOGY: Filling in a printed form. Boxes in a fixed order, then you
 *          sign at the bottom (the CRC) to vouch for everything above.
 *
 * INTERVIEW LINE:
 *   "The builder checks every input first and returns 0 on an oversized
 *    payload or a short output buffer, so a bad call can't write past
 *    the end of a buffer."
 */
#include <string.h>

#include "frame.h"
#include "crc16.h"

size_t frame_build(uint8_t type, uint8_t seq,
                   const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_size)
{
    /* 1. Check every input BEFORE writing a single byte. */
    if (out == NULL) {
        return 0;
    }
    if (payload_len > PROTO_MAX_PAYLOAD) {
        return 0;
    }
    if (payload_len > 0 && payload == NULL) {
        return 0;
    }
    size_t total = PROTO_HEADER_LEN + payload_len + PROTO_CRC_LEN;
    if (out_size < total) {
        return 0;
    }

    /* 2. Sync + header. */
    size_t i = 0;
    out[i++] = PROTO_SYNC1;
    out[i++] = PROTO_SYNC2;
    out[i++] = PROTO_VERSION;
    out[i++] = type;
    out[i++] = (uint8_t)payload_len;   /* safe: already checked <= 128 */
    out[i++] = seq;

    /* 3. Payload (skipped when empty). */
    if (payload_len > 0) {
        memcpy(&out[i], payload, payload_len);
        i += payload_len;
    }

    /* 4. CRC over everything after sync: out[2] .. out[i-1]. */
    uint16_t crc = crc16_ccitt_false(&out[2], i - 2);

    /* 5. CRC goes out low byte first (little-endian). */
    out[i++] = (uint8_t)(crc & 0xFFu);
    out[i++] = (uint8_t)(crc >> 8);

    return i;   /* equals total */
}
