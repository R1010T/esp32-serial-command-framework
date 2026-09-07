/*
 * test_parser.c - Unit tests for parser.c (the state machine)
 *
 * Each test feeds bytes in exactly like the UART would, then checks
 * what comes out. Several tests are the Session 1 questions as code.
 */
#include <string.h>

#include "unity.h"
#include "parser.h"
#include "frame.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------- */

/* Feed n bytes at one timestamp. Returns how many PARSE_OKs came out.
 * If last is not NULL, it gets the result of the final byte. */
static int feed(parser_t *p, const uint8_t *bytes, size_t n, uint32_t now_ms,
                frame_t *out, parse_result_t *last)
{
    int ok = 0;
    parse_result_t r = PARSE_IN_PROGRESS;
    for (size_t i = 0; i < n; i++) {
        r = parser_feed(p, bytes[i], now_ms, out);
        if (r == PARSE_OK) {
            ok++;
        }
    }
    if (last != NULL) {
        *last = r;
    }
    return ok;
}

/* Simple fixed-seed random bytes: same "random" sequence every run. */
static uint32_t rng_state = 12345u;
static uint8_t rng_byte(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (uint8_t)(rng_state >> 16);
}

/* Packets from protocol.md section 9 */
static const uint8_t PING_SEQ7[] = { 0xAA, 0x55, 0x01, 0x01, 0x00, 0x07, 0xA3, 0xB5 };
static const uint8_t NACK_SEQ8[] = { 0xAA, 0x55, 0x01, 0x81, 0x01, 0x08, 0x01, 0x69, 0xBE };

/* ---- Tests --------------------------------------------------------------- */

static void test_valid_ping_parses(void)
{
    parser_t p;
    frame_t f;
    parser_init(&p);

    /* Every byte except the last: still in progress. */
    for (size_t i = 0; i < sizeof PING_SEQ7 - 1; i++) {
        TEST_ASSERT_EQUAL_INT(PARSE_IN_PROGRESS, parser_feed(&p, PING_SEQ7[i], 0, &f));
    }
    /* The last byte completes it. */
    TEST_ASSERT_EQUAL_INT(PARSE_OK, parser_feed(&p, PING_SEQ7[sizeof PING_SEQ7 - 1], 0, &f));

    TEST_ASSERT_EQUAL_HEX8(0x01, f.version);
    TEST_ASSERT_EQUAL_HEX8(TYPE_PING, f.type);
    TEST_ASSERT_EQUAL_UINT8(0, f.length);
    TEST_ASSERT_EQUAL_UINT8(7, f.seq);
    TEST_ASSERT_EQUAL_HEX16(0xB5A3, f.crc);
    TEST_ASSERT_EQUAL_UINT32(1, p.stats.frames_ok);
}

static void test_packet_with_payload_parses(void)
{
    parser_t p;
    frame_t f;
    parse_result_t last;
    parser_init(&p);

    TEST_ASSERT_EQUAL_INT(1, feed(&p, NACK_SEQ8, sizeof NACK_SEQ8, 0, &f, &last));
    TEST_ASSERT_EQUAL_INT(PARSE_OK, last);
    TEST_ASSERT_EQUAL_HEX8(TYPE_NACK, f.type);
    TEST_ASSERT_EQUAL_UINT8(1, f.length);
    TEST_ASSERT_EQUAL_UINT8(8, f.seq);
    TEST_ASSERT_EQUAL_HEX8(NACK_UNKNOWN_TYPE, f.payload[0]);
}

/* Junk before a packet is skipped (includes a lone 55 and a lone AA). */
static void test_noise_before_sync_is_skipped(void)
{
    const uint8_t noise[] = { 0x00, 0xFF, 0x12, 0x55, 0xAA, 0x13 };
    parser_t p;
    frame_t f;
    parser_init(&p);

    TEST_ASSERT_EQUAL_INT(0, feed(&p, noise, sizeof noise, 0, &f, NULL));
    TEST_ASSERT_EQUAL_INT(1, feed(&p, PING_SEQ7, sizeof PING_SEQ7, 0, &f, NULL));
    TEST_ASSERT_EQUAL_UINT8(7, f.seq);
}

/* Session 1, Q1 edge case: AA AA 55 -> the second AA is the real start. */
static void test_double_sync_byte_still_syncs(void)
{
    const uint8_t extra_aa = 0xAA;
    parser_t p;
    frame_t f;
    parser_init(&p);

    TEST_ASSERT_EQUAL_INT(0, feed(&p, &extra_aa, 1, 0, &f, NULL));
    TEST_ASSERT_EQUAL_INT(1, feed(&p, PING_SEQ7, sizeof PING_SEQ7, 0, &f, NULL));
}

/* Session 1, Q2 as code: Length = 200 is rejected on the Length byte itself. */
static void test_length_over_max_rejected_then_recovers(void)
{
    const uint8_t bad_hdr[] = { 0xAA, 0x55, 0x01, 0x01, 200 };  /* sync, ver, type, LENGTH */
    parser_t p;
    frame_t f;
    parse_result_t last;
    parser_init(&p);

    feed(&p, bad_hdr, sizeof bad_hdr, 0, &f, &last);
    TEST_ASSERT_EQUAL_INT(PARSE_ERR_LEN, last);
    TEST_ASSERT_EQUAL_UINT32(1, p.stats.err_len);

    /* Back to hunting: the next good packet still gets through. */
    TEST_ASSERT_EQUAL_INT(1, feed(&p, PING_SEQ7, sizeof PING_SEQ7, 0, &f, NULL));
}

/* Boundary: Length 128 is accepted, 129 is rejected. */
static void test_length_boundary_128_ok_129_rejected(void)
{
    uint8_t payload[PROTO_MAX_PAYLOAD];
    uint8_t pkt[PROTO_MAX_FRAME];
    parser_t p;
    frame_t f;
    parse_result_t last;

    for (unsigned i = 0; i < sizeof payload; i++) {
        payload[i] = (uint8_t)i;
    }
    size_t n = frame_build(TYPE_SET_CONFIG, 3, payload, sizeof payload, pkt, sizeof pkt);
    TEST_ASSERT_EQUAL_UINT32(PROTO_MAX_FRAME, n);

    parser_init(&p);
    TEST_ASSERT_EQUAL_INT(1, feed(&p, pkt, n, 0, &f, NULL));
    TEST_ASSERT_EQUAL_UINT8(128, f.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, sizeof payload);

    /* Length = 129: frame_build refuses it, so the header is hand-written. */
    const uint8_t hdr_129[] = { 0xAA, 0x55, 0x01, TYPE_SET_CONFIG, 129 };
    feed(&p, hdr_129, sizeof hdr_129, 0, &f, &last);
    TEST_ASSERT_EQUAL_INT(PARSE_ERR_LEN, last);
}

/* Session 1, Q4 as code: one flipped bit -> CRC mismatch -> dropped. */
static void test_bit_flip_rejected_then_recovers(void)
{
    uint8_t damaged[sizeof PING_SEQ7];
    parser_t p;
    frame_t f;
    parse_result_t last;

    memcpy(damaged, PING_SEQ7, sizeof damaged);
    damaged[3] ^= 0x02;          /* Type 0x01 -> 0x03: exactly one bit flipped */

    parser_init(&p);
    TEST_ASSERT_EQUAL_INT(0, feed(&p, damaged, sizeof damaged, 0, &f, &last));
    TEST_ASSERT_EQUAL_INT(PARSE_ERR_CRC, last);
    TEST_ASSERT_EQUAL_UINT32(1, p.stats.err_crc);

    /* The next good packet still gets through, as a PING (not GET_CONFIG). */
    TEST_ASSERT_EQUAL_INT(1, feed(&p, PING_SEQ7, sizeof PING_SEQ7, 0, &f, NULL));
    TEST_ASSERT_EQUAL_HEX8(TYPE_PING, f.type);
}

/* AA 55 inside the payload must not confuse the parser. */
static void test_sync_bytes_inside_payload(void)
{
    const uint8_t payload[] = { 0xAA, 0x55, 0xAA, 0x55 };
    uint8_t pkt[PROTO_MAX_FRAME];
    parser_t p;
    frame_t f;

    size_t n = frame_build(TYPE_SET_CONFIG, 9, payload, sizeof payload, pkt, sizeof pkt);
    parser_init(&p);

    TEST_ASSERT_EQUAL_INT(1, feed(&p, pkt, n, 0, &f, NULL));
    TEST_ASSERT_EQUAL_UINT8(4, f.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, sizeof payload);
}

/* Two packets glued together, no gap: both come out. */
static void test_back_to_back_packets(void)
{
    uint8_t stream[sizeof PING_SEQ7 + sizeof NACK_SEQ8];
    parser_t p;
    frame_t f;

    memcpy(stream, PING_SEQ7, sizeof PING_SEQ7);
    memcpy(stream + sizeof PING_SEQ7, NACK_SEQ8, sizeof NACK_SEQ8);

    parser_init(&p);
    TEST_ASSERT_EQUAL_INT(2, feed(&p, stream, sizeof stream, 0, &f, NULL));
    TEST_ASSERT_EQUAL_HEX8(TYPE_NACK, f.type);   /* the last one out */
    TEST_ASSERT_EQUAL_UINT32(2, p.stats.frames_ok);
}

/* A packet stalls half-way for > 50 ms: dropped. A fresh packet then parses. */
static void test_stalled_packet_times_out(void)
{
    parser_t p;
    frame_t f;
    parse_result_t last;
    parser_init(&p);

    feed(&p, PING_SEQ7, 4, 1000, &f, NULL);       /* first half arrives at t = 1000 ms */

    /* 100 ms later a NEW packet starts. Its first byte triggers the timeout. */
    TEST_ASSERT_EQUAL_INT(PARSE_ERR_TIMEOUT, parser_feed(&p, PING_SEQ7[0], 1100, &f));
    TEST_ASSERT_EQUAL_UINT32(1, p.stats.err_timeout);

    /* That same byte (AA) was kept as a fresh start: the rest completes it. */
    TEST_ASSERT_EQUAL_INT(1, feed(&p, PING_SEQ7 + 1, sizeof PING_SEQ7 - 1, 1100, &f, &last));
    TEST_ASSERT_EQUAL_INT(PARSE_OK, last);
}

/* Stress: 1000 good packets, each after random junk. All 1000 must come out. */
static void test_1000_packets_after_random_junk(void)
{
    parser_t p;
    frame_t f;
    uint32_t t = 0;
    int good = 0;

    memset(&f, 0, sizeof f);
    parser_init(&p);

    for (int round = 0; round < 1000; round++) {
        int junk = rng_byte() % 32;                 /* 0..31 junk bytes */
        for (int j = 0; j < junk; j++) {
            parser_feed(&p, rng_byte(), t, &f);
        }
        t += 60;                                    /* quiet gap > 50 ms */

        good += feed(&p, PING_SEQ7, sizeof PING_SEQ7, t, &f, NULL);
        TEST_ASSERT_TRUE(f.length <= PROTO_MAX_PAYLOAD);
        t += 60;
    }
    TEST_ASSERT_EQUAL_INT(1000, good);
}

/* Fuzz: 100,000 random bytes. No crash, never past the buffer, still recovers. */
static void test_random_bytes_never_break_parser(void)
{
    parser_t p;
    frame_t f;

    memset(&f, 0, sizeof f);
    parser_init(&p);

    for (int i = 0; i < 100000; i++) {
        if (parser_feed(&p, rng_byte(), 0, &f) == PARSE_OK) {
            TEST_ASSERT_TRUE(f.length <= PROTO_MAX_PAYLOAD);
        }
    }

    /* After a quiet gap, a good packet must still get through. */
    TEST_ASSERT_EQUAL_INT(1, feed(&p, PING_SEQ7, sizeof PING_SEQ7, 1000, &f, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_ping_parses);
    RUN_TEST(test_packet_with_payload_parses);
    RUN_TEST(test_noise_before_sync_is_skipped);
    RUN_TEST(test_double_sync_byte_still_syncs);
    RUN_TEST(test_length_over_max_rejected_then_recovers);
    RUN_TEST(test_length_boundary_128_ok_129_rejected);
    RUN_TEST(test_bit_flip_rejected_then_recovers);
    RUN_TEST(test_sync_bytes_inside_payload);
    RUN_TEST(test_back_to_back_packets);
    RUN_TEST(test_stalled_packet_times_out);
    RUN_TEST(test_1000_packets_after_random_junk);
    RUN_TEST(test_random_bytes_never_break_parser);
    return UNITY_END();
}
