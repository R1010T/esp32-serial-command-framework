/*
 * parser.h - Byte-by-byte packet parser (state machine)
 *
 * Feed it one received byte at a time. It returns PARSE_OK when a
 * complete, CRC-checked packet is ready, and copies that packet into
 * a frame_t you own.
 */
#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include "frame.h"

/* Max gap between two bytes inside one packet. Longer = packet dropped. */
#define PARSER_TIMEOUT_MS 50u

/* What one call to parser_feed() reports. */
typedef enum {
    PARSE_IN_PROGRESS = 0,  /* byte taken, packet not finished yet (or noise skipped) */
    PARSE_OK,               /* complete packet passed CRC, copied to *out */
    PARSE_ERR_LEN,          /* Length > 128: packet rejected before storing anything */
    PARSE_ERR_CRC,          /* CRC mismatch: packet dropped */
    PARSE_ERR_TIMEOUT       /* packet stalled mid-way: dropped (this byte still processed) */
} parse_result_t;

/* Where the parser is inside a packet. It remembers this between bytes. */
typedef enum {
    PS_WAIT_SYNC1 = 0,  /* looking for AA */
    PS_WAIT_SYNC2,      /* got AA, looking for 55 */
    PS_HEADER,          /* reading Version, Type, Length, Seq */
    PS_PAYLOAD,         /* reading Length payload bytes */
    PS_CRC_LO,          /* reading CRC low byte */
    PS_CRC_HI           /* reading CRC high byte, then check */
} parser_state_t;

/* Running totals, reported later in STATUS. */
typedef struct {
    uint32_t frames_ok;
    uint32_t err_len;
    uint32_t err_crc;
    uint32_t err_timeout;
} parser_stats_t;

/* All parser memory. Fixed size: no malloc anywhere. */
typedef struct {
    parser_state_t state;
    uint8_t        hdr_idx;       /* next header byte: 0=Version 1=Type 2=Length 3=Seq */
    uint8_t        pay_idx;       /* payload bytes stored so far */
    uint16_t       crc_calc;      /* CRC computed as bytes arrive */
    uint8_t        crc_lo;        /* first CRC byte received (low byte) */
    uint32_t       last_byte_ms;  /* arrival time of the previous byte */
    frame_t        work;          /* packet being assembled */
    parser_stats_t stats;
} parser_t;

/* Reset everything, stats included. Call once before the first byte. */
void parser_init(parser_t *p);

/*
 * Feed one byte.
 *   now_ms: current time in milliseconds (any clock that counts up;
 *           wrap-around at 2^32 is handled).
 *   out:    where a finished packet is copied. Written ONLY on PARSE_OK.
 * p and out must not be NULL.
 */
parse_result_t parser_feed(parser_t *p, uint8_t byte, uint32_t now_ms, frame_t *out);

#endif /* PARSER_H */
