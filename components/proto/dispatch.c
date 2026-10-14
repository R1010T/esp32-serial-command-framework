/*
 * dispatch.c - Command handler: one good packet in, one reply out
 *
 * WHAT:    Takes a packet the parser has already checked (CRC passed),
 *          decides what it asks for, and builds the reply packet.
 * WHY:     The parser only answers "is this packet intact?". The
 *          dispatcher answers "what do I do about it?": run the command,
 *          refuse it with an error code (NACK), or spot a repeat and
 *          resend the stored reply instead of running it twice.
 * ANALOGY: A bank teller. The parser checks the cheque isn't torn; the
 *          teller reads it, pays it, or hands it back with a reason, and
 *          won't pay cheque #7 twice.
 *
 * Rules implemented here: docs/protocol.md, sections 4, 5, 7 and 10.
 *
 * INTERVIEW LINE:
 *   "The dispatcher is pure logic with no hardware calls, so it's unit
 *    tested on the laptop. It NACKs bad versions, bad payloads and
 *    unknown types with an error code, and uses Seq plus CRC to answer
 *    retries from a stored reply instead of running a command twice."
 */
#include <string.h>

#include "dispatch.h"

/* Write a 32-bit number low byte first (little-endian), as the laptop expects. */
static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Build a NACK carrying a 1-byte error code (protocol.md section 5). */
static size_t build_nack(dispatch_t *d, uint8_t seq, uint8_t code,
                         uint8_t *out, size_t out_size)
{
    d->stats.nack_sent++;
    return frame_build(TYPE_NACK, seq, &code, 1, out, out_size);
}

/* Build the STATUS reply: 8 counters, 4 bytes each (protocol.md section 10). */
static size_t build_status(const dispatch_t *d, uint8_t seq,
                           const parser_stats_t *pstats, uint32_t now_ms,
                           uint8_t *out, size_t out_size)
{
    uint8_t p[STATUS_PAYLOAD_LEN];
    parser_stats_t zero;

    if (pstats == NULL) {              /* no parser counters given: report zeros */
        memset(&zero, 0, sizeof zero);
        pstats = &zero;
    }
    put_u32le(&p[0],  now_ms);
    put_u32le(&p[4],  pstats->frames_ok);
    put_u32le(&p[8],  pstats->err_len);
    put_u32le(&p[12], pstats->err_crc);
    put_u32le(&p[16], pstats->err_timeout);
    put_u32le(&p[20], d->stats.dup);
    put_u32le(&p[24], d->stats.seq_gap);
    put_u32le(&p[28], d->stats.nack_sent);

    return frame_build(TYPE_STATUS, seq, p, sizeof p, out, out_size);
}

void dispatch_init(dispatch_t *d)
{
    memset(d, 0, sizeof(*d));
}

size_t dispatch_handle(dispatch_t *d, const frame_t *in,
                       const parser_stats_t *pstats, uint32_t now_ms,
                       uint8_t *out, size_t out_size)
{
    if (d == NULL || in == NULL || out == NULL || out_size < PROTO_MAX_FRAME) {
        return 0;
    }

    /*
     * 1. A repeat? (Session 1, Q3.) Same Seq AND same CRC as the last
     *    packet: the laptop never got our reply and sent the same packet
     *    again. Resend the stored reply; don't run the command twice.
     */
    if (d->have_last && in->seq == d->last_seq && in->crc == d->last_crc) {
        d->stats.dup++;
        memcpy(out, d->last_reply, d->last_reply_len);
        return d->last_reply_len;
    }

    /* 2. Seq should be last + 1 (255 wraps to 0). If not, count the gap and carry on. */
    if (d->have_last && in->seq != (uint8_t)(d->last_seq + 1u)) {
        d->stats.seq_gap++;
    }

    /* 3. Run the command and build its reply. A reply carries the command's Seq. */
    size_t n;
    if (in->version != PROTO_VERSION) {
        n = build_nack(d, in->seq, NACK_BAD_VERSION, out, out_size);
    } else {
        switch (in->type) {
        case TYPE_PING:                                /* "are you there?" */
            if (in->length != 0) {
                n = build_nack(d, in->seq, NACK_BAD_PAYLOAD, out, out_size);
            } else {
                n = frame_build(TYPE_ACK, in->seq, NULL, 0, out, out_size);
            }
            break;

        case TYPE_GET_STATUS:                          /* "send me your counters" */
            if (in->length != 0) {
                n = build_nack(d, in->seq, NACK_BAD_PAYLOAD, out, out_size);
            } else {
                n = build_status(d, in->seq, pstats, now_ms, out, out_size);
            }
            break;

        default:   /* not a command, or one not built yet (protocol.md section 4) */
            n = build_nack(d, in->seq, NACK_UNKNOWN_TYPE, out, out_size);
            break;
        }
    }
    if (n == 0) {
        return 0;   /* unreachable with a 136-byte buffer; nothing stored */
    }

    /* 4. Remember this packet and its reply, so a repeat can be answered (step 1). */
    d->have_last      = true;
    d->last_seq       = in->seq;
    d->last_crc       = in->crc;
    memcpy(d->last_reply, out, n);
    d->last_reply_len = n;
    return n;
}
