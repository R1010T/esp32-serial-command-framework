/*
 * frame.h - Protocol constants + packet builder
 *
 * Packet layout (docs/protocol.md, section 2):
 *
 *   AA 55 | Version | Type | Length | Seq | Payload (0-128) | CRC lo | CRC hi
 *   <sync>  <----------- header ----------> <---- data ---->  <-- CRC16 -->
 *
 * The CRC covers everything after sync: Version .. end of Payload.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>

/* ---- Fixed values -------------------------------------------------------- */
#define PROTO_SYNC1        0xAAu
#define PROTO_SYNC2        0x55u
#define PROTO_VERSION      0x01u

/* ---- Sizes --------------------------------------------------------------- */
#define PROTO_MAX_PAYLOAD  128u   /* any Length above this is rejected */
#define PROTO_HEADER_LEN   6u     /* sync(2) + version + type + length + seq */
#define PROTO_CRC_LEN      2u
#define PROTO_MIN_FRAME    (PROTO_HEADER_LEN + PROTO_CRC_LEN)                      /*   8 */
#define PROTO_MAX_FRAME    (PROTO_HEADER_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN)  /* 136 */

/* ---- Types: commands (laptop -> ESP32), high bit clear ------------------- */
#define TYPE_PING          0x01u
#define TYPE_GET_STATUS    0x02u
#define TYPE_GET_CONFIG    0x03u
#define TYPE_SET_CONFIG    0x04u
#define TYPE_START_STREAM  0x05u
#define TYPE_STOP_STREAM   0x06u
#define TYPE_INJECT_FAULT  0x07u
#define TYPE_CLEAR_LOG     0x08u

/* ---- Types: responses (ESP32 -> laptop), high bit set -------------------- */
#define TYPE_ACK           0x80u
#define TYPE_NACK          0x81u
#define TYPE_STATUS        0x82u
#define TYPE_TELEMETRY     0x83u

/* ---- NACK error codes (the 1-byte payload of a NACK) --------------------- */
#define NACK_UNKNOWN_TYPE  0x01u
#define NACK_BAD_VERSION   0x02u
#define NACK_BAD_PAYLOAD   0x03u
#define NACK_BUSY          0x04u

/* One decoded packet, as the parser hands it over. */
typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  length;                       /* number of valid bytes in payload[] */
    uint8_t  seq;
    uint16_t crc;                          /* CRC as received; used later for duplicate checks */
    uint8_t  payload[PROTO_MAX_PAYLOAD];
} frame_t;

/*
 * Build one complete packet into out[].
 *
 * Returns the packet length in bytes (8..136), or 0 if:
 *   - out is NULL
 *   - payload_len > PROTO_MAX_PAYLOAD
 *   - payload is NULL but payload_len > 0
 *   - out_size is too small for the packet
 * On 0, nothing is written to out.
 */
size_t frame_build(uint8_t type, uint8_t seq,
                   const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_size);

#endif /* FRAME_H */
