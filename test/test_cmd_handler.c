/*
 * test_dispatch.c - Unit tests for dispatch.c (command handler)
 *
 * Expected reply bytes come straight from docs/protocol.md, section 9.
 */
#include <string.h>

#include "unity.h"
#include "dispatch.h"
#include "parser.h"
#include "frame.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------- */

/* Build a command packet and run it through the real parser, exactly as the
 * ESP32 does: bytes in, frame_t out. */
static frame_t make_cmd(uint8_t type, uint8_t seq, const uint8_t *payload, size_t len)
{
    uint8_t pkt[PROTO_MAX_FRAME];
    size_t n = frame_build(type, seq, payload, len, pkt, sizeof pkt);
    TEST_ASSERT_TRUE(n > 0);

    parser_t p;
    frame_t f;
    parse_result_t r = PARSE_IN_PROGRESS;
    parser_init(&p);
    for (size_t i = 0; i < n; i++) {
        r = parser_feed(&p, pkt[i], 0, &f);
    }
    TEST_ASSERT_EQUAL_INT(PARSE_OK, r);
    return f;
}

/* Read a 32-bit little-endian number out of a reply payload. */
static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- Tests --------------------------------------------------------------- */

/* PING Seq 7 -> exactly the ACK bytes in protocol.md section 9. */
static void test_ping_gets_ack_matching_spec(void)
{
    const uint8_t expected[] = { 0xAA, 0x55, 0x01, 0x80, 0x00, 0x07, 0xC9, 0xB9 };
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t cmd = make_cmd(TYPE_PING, 7, NULL, 0);
    size_t n = dispatch_handle(&d, &cmd, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

/* Unknown type 0x09, Seq 8 -> exactly the NACK bytes in protocol.md section 9. */
static void test_unknown_type_gets_nack_matching_spec(void)
{
    const uint8_t expected[] = { 0xAA, 0x55, 0x01, 0x81, 0x01, 0x08, 0x01, 0x69, 0xBE };
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t cmd = make_cmd(0x09, 8, NULL, 0);
    size_t n = dispatch_handle(&d, &cmd, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
    TEST_ASSERT_EQUAL_UINT32(1, d.stats.nack_sent);
}

/* PING must be empty. A PING with data -> NACK BAD_PAYLOAD. */
static void test_ping_with_payload_gets_bad_payload(void)
{
    const uint8_t junk[] = { 0x42 };
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t cmd = make_cmd(TYPE_PING, 1, junk, sizeof junk);
    dispatch_handle(&d, &cmd, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_HEX8(TYPE_NACK, out[3]);
    TEST_ASSERT_EQUAL_HEX8(NACK_BAD_PAYLOAD, out[6]);
}

/* A packet from a future format version -> NACK BAD_VERSION. */
static void test_wrong_version_gets_bad_version(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t cmd = make_cmd(TYPE_PING, 1, NULL, 0);
    cmd.version = 0x02;
    dispatch_handle(&d, &cmd, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_HEX8(TYPE_NACK, out[3]);
    TEST_ASSERT_EQUAL_HEX8(NACK_BAD_VERSION, out[6]);
}

/* GET_STATUS -> 32-byte payload, every counter in its place (protocol.md section 10). */
static void test_status_reports_every_counter(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    parser_stats_t ps = { .frames_ok = 10, .err_len = 2, .err_crc = 3, .err_timeout = 4 };
    dispatch_init(&d);
    d.stats.dup = 5;
    d.stats.seq_gap = 6;
    d.stats.nack_sent = 7;

    frame_t cmd = make_cmd(TYPE_GET_STATUS, 1, NULL, 0);
    size_t n = dispatch_handle(&d, &cmd, &ps, 123456u, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(PROTO_HEADER_LEN + STATUS_PAYLOAD_LEN + PROTO_CRC_LEN, n);
    TEST_ASSERT_EQUAL_HEX8(TYPE_STATUS, out[3]);
    TEST_ASSERT_EQUAL_UINT8(STATUS_PAYLOAD_LEN, out[4]);
    const uint8_t *p = &out[PROTO_HEADER_LEN];
    TEST_ASSERT_EQUAL_UINT32(123456u, get_u32le(&p[0]));   /* uptime_ms   */
    TEST_ASSERT_EQUAL_UINT32(10, get_u32le(&p[4]));        /* frames_ok   */
    TEST_ASSERT_EQUAL_UINT32(2,  get_u32le(&p[8]));        /* err_len     */
    TEST_ASSERT_EQUAL_UINT32(3,  get_u32le(&p[12]));       /* err_crc     */
    TEST_ASSERT_EQUAL_UINT32(4,  get_u32le(&p[16]));       /* err_timeout */
    TEST_ASSERT_EQUAL_UINT32(5,  get_u32le(&p[20]));       /* dup         */
    TEST_ASSERT_EQUAL_UINT32(6,  get_u32le(&p[24]));       /* seq_gap     */
    TEST_ASSERT_EQUAL_UINT32(7,  get_u32le(&p[28]));       /* nack_sent   */
}

/* Session 1, Q3 as code: the same packet twice -> the stored reply, NOT a fresh run.
 * Proof: the uptime inside the second STATUS is the old one (1000), not 2000. */
static void test_repeat_gets_stored_reply_not_a_rerun(void)
{
    dispatch_t d;
    uint8_t first[PROTO_MAX_FRAME];
    uint8_t second[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t cmd = make_cmd(TYPE_GET_STATUS, 9, NULL, 0);
    size_t n1 = dispatch_handle(&d, &cmd, NULL, 1000u, first, sizeof first);
    size_t n2 = dispatch_handle(&d, &cmd, NULL, 2000u, second, sizeof second);

    TEST_ASSERT_EQUAL_UINT32(n1, n2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first, second, n1);
    TEST_ASSERT_EQUAL_UINT32(1000u, get_u32le(&second[PROTO_HEADER_LEN]));
    TEST_ASSERT_EQUAL_UINT32(1, d.stats.dup);
}

/* Same Seq but a different command (different CRC) is NOT a repeat. */
static void test_same_seq_different_command_is_new(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t ping = make_cmd(TYPE_PING, 5, NULL, 0);
    frame_t status = make_cmd(TYPE_GET_STATUS, 5, NULL, 0);
    dispatch_handle(&d, &ping, NULL, 0, out, sizeof out);
    dispatch_handle(&d, &status, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_HEX8(TYPE_STATUS, out[3]);
    TEST_ASSERT_EQUAL_UINT32(0, d.stats.dup);
}

/* Seq 1 then 4: two commands went missing -> one gap counted, command still runs. */
static void test_seq_gap_counted(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t a = make_cmd(TYPE_PING, 1, NULL, 0);
    frame_t b = make_cmd(TYPE_PING, 4, NULL, 0);
    frame_t c = make_cmd(TYPE_PING, 5, NULL, 0);
    dispatch_handle(&d, &a, NULL, 0, out, sizeof out);
    dispatch_handle(&d, &b, NULL, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(TYPE_ACK, out[3]);
    dispatch_handle(&d, &c, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(1, d.stats.seq_gap);
}

/* 255 -> 0 is the normal wrap, not a gap. */
static void test_seq_wrap_is_not_a_gap(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t a = make_cmd(TYPE_PING, 255, NULL, 0);
    frame_t b = make_cmd(TYPE_PING, 0, NULL, 0);
    dispatch_handle(&d, &a, NULL, 0, out, sizeof out);
    dispatch_handle(&d, &b, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(0, d.stats.seq_gap);
}

/* After boot there is no "last Seq": the first command is fine with any Seq. */
static void test_first_command_any_seq(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t a = make_cmd(TYPE_PING, 200, NULL, 0);
    dispatch_handle(&d, &a, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(0, d.stats.seq_gap);
    TEST_ASSERT_EQUAL_UINT32(0, d.stats.dup);
}

/* Every reply must itself be a valid packet: run it back through the parser. */
static void test_reply_parses_back(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME];
    dispatch_init(&d);

    frame_t cmd = make_cmd(TYPE_GET_STATUS, 33, NULL, 0);
    size_t n = dispatch_handle(&d, &cmd, NULL, 42u, out, sizeof out);

    parser_t p;
    frame_t f;
    parse_result_t r = PARSE_IN_PROGRESS;
    parser_init(&p);
    for (size_t i = 0; i < n; i++) {
        r = parser_feed(&p, out[i], 0, &f);
    }
    TEST_ASSERT_EQUAL_INT(PARSE_OK, r);
    TEST_ASSERT_EQUAL_HEX8(TYPE_STATUS, f.type);
    TEST_ASSERT_EQUAL_UINT8(33, f.seq);
}

/* Bad arguments: nothing written, 0 returned. */
static void test_rejects_small_output_buffer(void)
{
    dispatch_t d;
    uint8_t out[PROTO_MAX_FRAME - 1];
    dispatch_init(&d);
    memset(out, 0xEE, sizeof out);

    frame_t cmd = make_cmd(TYPE_PING, 1, NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, dispatch_handle(&d, &cmd, NULL, 0, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ping_gets_ack_matching_spec);
    RUN_TEST(test_unknown_type_gets_nack_matching_spec);
    RUN_TEST(test_ping_with_payload_gets_bad_payload);
    RUN_TEST(test_wrong_version_gets_bad_version);
    RUN_TEST(test_status_reports_every_counter);
    RUN_TEST(test_repeat_gets_stored_reply_not_a_rerun);
    RUN_TEST(test_same_seq_different_command_is_new);
    RUN_TEST(test_seq_gap_counted);
    RUN_TEST(test_seq_wrap_is_not_a_gap);
    RUN_TEST(test_first_command_any_seq);
    RUN_TEST(test_reply_parses_back);
    RUN_TEST(test_rejects_small_output_buffer);
    return UNITY_END();
}
