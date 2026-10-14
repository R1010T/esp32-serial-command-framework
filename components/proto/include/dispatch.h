/*
 * dispatch.h - Command handler: one good packet in, one reply out
 *
 * Rules implemented: docs/protocol.md, sections 4, 5, 7 and 10.
 * Pure logic, no hardware: the same file runs on the ESP32 and in the
 * laptop unit tests (test/test_dispatch.c).
 */
#ifndef DISPATCH_H
#define DISPATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "parser.h"

/* Size of the STATUS reply's payload: 8 numbers x 4 bytes (protocol.md section 10). */
#define STATUS_PAYLOAD_LEN 32u

/* Counters the dispatcher keeps (the parser keeps its own). */
typedef struct {
    uint32_t dup;        /* repeats answered from the stored reply */
    uint32_t seq_gap;    /* Seq jumped: commands lost on the way, or a new laptop run */
    uint32_t nack_sent;  /* NACK replies built */
} dispatch_stats_t;

/* All dispatcher memory. Fixed size: no malloc anywhere. */
typedef struct {
    bool             have_last;                      /* false until the first packet after boot */
    uint8_t          last_seq;                       /* Seq of the last packet handled */
    uint16_t         last_crc;                       /* its CRC: same Seq + same CRC = a repeat */
    uint8_t          last_reply[PROTO_MAX_FRAME];    /* the reply sent for it */
    size_t           last_reply_len;
    dispatch_stats_t stats;
} dispatch_t;

/* Reset everything, stats included. Call once at boot. */
void dispatch_init(dispatch_t *d);

/*
 * Handle one packet that passed CRC (from parser_feed) and build the reply.
 *   pstats:   the parser's counters, reported by GET_STATUS (may be NULL)
 *   now_ms:   uptime in ms, reported by GET_STATUS
 *   out:      the reply goes here; needs at least PROTO_MAX_FRAME bytes
 * Returns the reply length (8..136 bytes), or 0 if there is nothing to send.
 */
size_t dispatch_handle(dispatch_t *d, const frame_t *in,
                       const parser_stats_t *pstats, uint32_t now_ms,
                       uint8_t *out, size_t out_size);

#endif /* DISPATCH_H */
