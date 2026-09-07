/*
 * parser.c - Byte-by-byte packet parser (state machine)
 *
 * WHAT:    Receives bytes one at a time and rebuilds packets from them.
 * WHY:     UART delivers a nonstop stream. The only "new message" marker
 *          is our sync bytes. Noise can corrupt, drop or add bytes. The
 *          parser must find packets, reject damaged ones, and never write
 *          outside its buffer, whatever arrives.
 * ANALOGY: A mail sorter handed a letter one character at a time.
 *          1. Ignore everything until the "START" stamp (AA 55).
 *          2. Read the address label (header: 4 bytes).
 *          3. Read exactly as many pages as the label says (payload).
 *          4. Check the seal (CRC). Broken seal -> bin it.
 *          A "state machine" is just the sorter remembering which of
 *          these 4 steps it is on between characters.
 *
 * Rules implemented here: docs/protocol.md, section 6.
 *
 * INTERVIEW LINE:
 *   "Byte-wise state machine with a fixed 128-byte buffer. Length is
 *    checked before any payload byte is stored, the CRC is computed on
 *    the fly, and a 50 ms inter-byte timeout drops stalled packets.
 *    It never allocates."
 */
#include <string.h>

#include "parser.h"
#include "crc16.h"

/* Go back to hunting for AA 55. Stats are kept. */
static void reset_to_hunt(parser_t *p)
{
    p->state   = PS_WAIT_SYNC1;
    p->hdr_idx = 0;
    p->pay_idx = 0;
}

void parser_init(parser_t *p)
{
    memset(p, 0, sizeof(*p));   /* zero everything, stats included */
    reset_to_hunt(p);
}

parse_result_t parser_feed(parser_t *p, uint8_t byte, uint32_t now_ms, frame_t *out)
{
    parse_result_t result = PARSE_IN_PROGRESS;

    /*
     * Inter-byte timeout (protocol.md section 6, rule 6).
     * Only once a packet has really started (sync confirmed): if the gap
     * since the previous byte is too long, the rest of that packet is
     * never coming. Drop it, then treat THIS byte as a fresh start.
     * Unsigned subtraction handles the clock wrapping past 2^32.
     */
    if (p->state != PS_WAIT_SYNC1 && p->state != PS_WAIT_SYNC2 &&
        (uint32_t)(now_ms - p->last_byte_ms) > PARSER_TIMEOUT_MS) {
        p->stats.err_timeout++;
        reset_to_hunt(p);
        result = PARSE_ERR_TIMEOUT;
    }
    p->last_byte_ms = now_ms;

    switch (p->state) {

    case PS_WAIT_SYNC1:
        /* Anything that isn't AA is noise: ignore it. */
        if (byte == PROTO_SYNC1) {
            p->state = PS_WAIT_SYNC2;
        }
        break;

    case PS_WAIT_SYNC2:
        if (byte == PROTO_SYNC2) {
            /* AA 55 found: a packet starts. Fresh CRC for this packet. */
            p->state    = PS_HEADER;
            p->hdr_idx  = 0;
            p->crc_calc = CRC16_INIT;
        } else if (byte != PROTO_SYNC1) {
            /* AA then junk: false alarm, back to hunting. */
            p->state = PS_WAIT_SYNC1;
        }
        /* AA AA: stay here. The second AA may be the real start. */
        break;

    case PS_HEADER:
        p->crc_calc = crc16_update(p->crc_calc, byte);  /* header is covered by the CRC */
        if (p->hdr_idx == 0) {
            p->work.version = byte;
        } else if (p->hdr_idx == 1) {
            p->work.type = byte;
        } else if (p->hdr_idx == 2) {
            /*
             * Session 1, Q2: check Length BEFORE storing anything.
             * A noisy 200 here would otherwise make us write 72 bytes
             * past the end of payload[128]: buffer overflow.
             */
            if (byte > PROTO_MAX_PAYLOAD) {
                p->stats.err_len++;
                reset_to_hunt(p);
                return PARSE_ERR_LEN;
            }
            p->work.length = byte;
        } else {
            p->work.seq = byte;
        }
        p->hdr_idx++;

        if (p->hdr_idx == 4) {                 /* all 4 header bytes are in */
            p->pay_idx = 0;
            p->state = (p->work.length == 0) ? PS_CRC_LO : PS_PAYLOAD;
        }
        break;

    case PS_PAYLOAD:
        p->crc_calc = crc16_update(p->crc_calc, byte);
        /* Safe: pay_idx < length, and length <= 128 was checked above. */
        p->work.payload[p->pay_idx] = byte;
        p->pay_idx++;
        if (p->pay_idx == p->work.length) {
            p->state = PS_CRC_LO;
        }
        break;

    case PS_CRC_LO:
        p->crc_lo = byte;                      /* CRC arrives low byte first */
        p->state  = PS_CRC_HI;
        break;

    case PS_CRC_HI: {
        uint16_t crc_rx = (uint16_t)(p->crc_lo | ((uint16_t)byte << 8));
        reset_to_hunt(p);                      /* packet is finished either way */

        if (crc_rx != p->crc_calc) {
            /* Session 1, Q4: damaged packet. Drop it, no reply. */
            p->stats.err_crc++;
            return PARSE_ERR_CRC;
        }

        p->work.crc = crc_rx;
        *out = p->work;                        /* hand a copy to the caller */
        p->stats.frames_ok++;
        return PARSE_OK;
    }

    default:
        /* Unreachable. If memory got corrupted, recover instead of hanging. */
        reset_to_hunt(p);
        break;
    }

    return result;
}
