/*
 * test_frame.c - Unit tests for frame.c (packet builder)
 *
 * Expected bytes come straight from docs/protocol.md, section 9.
 */
#include <string.h>

#include "unity.h"
#include "frame.h"

void setUp(void) {}
void tearDown(void) {}

static void test_build_ping_matches_spec(void)
{
    const uint8_t expected[] = { 0xAA, 0x55, 0x01, 0x01, 0x00, 0x07, 0xA3, 0xB5 };
    uint8_t out[PROTO_MAX_FRAME];

    size_t n = frame_build(TYPE_PING, 7, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_build_ack_matches_spec(void)
{
    const uint8_t expected[] = { 0xAA, 0x55, 0x01, 0x80, 0x00, 0x07, 0xC9, 0xB9 };
    uint8_t out[PROTO_MAX_FRAME];

    size_t n = frame_build(TYPE_ACK, 7, NULL, 0, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_build_nack_with_payload_matches_spec(void)
{
    const uint8_t code = NACK_UNKNOWN_TYPE;
    const uint8_t expected[] = { 0xAA, 0x55, 0x01, 0x81, 0x01, 0x08, 0x01, 0x69, 0xBE };
    uint8_t out[PROTO_MAX_FRAME];

    size_t n = frame_build(TYPE_NACK, 8, &code, 1, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

/* Boundary: exactly 128 payload bytes is allowed -> 136-byte packet. */
static void test_build_max_payload_128(void)
{
    uint8_t payload[PROTO_MAX_PAYLOAD] = { 0 };
    uint8_t out[PROTO_MAX_FRAME];

    size_t n = frame_build(TYPE_SET_CONFIG, 1, payload, sizeof payload, out, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(PROTO_MAX_FRAME, n);
    TEST_ASSERT_EQUAL_HEX8(128, out[4]);          /* the Length byte */
}

/* Boundary + 1: 129 bytes must be refused, and nothing written. */
static void test_build_rejects_payload_129(void)
{
    uint8_t payload[PROTO_MAX_PAYLOAD + 1] = { 0 };
    uint8_t out[PROTO_MAX_FRAME + 8];
    memset(out, 0xEE, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(0, frame_build(TYPE_SET_CONFIG, 1, payload, sizeof payload,
                                            out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[0]);         /* untouched */
}

/* Output buffer one byte too small for a PING (a PING needs 8). */
static void test_build_rejects_small_output_buffer(void)
{
    uint8_t out[PROTO_MIN_FRAME - 1];
    memset(out, 0xEE, sizeof out);

    TEST_ASSERT_EQUAL_UINT32(0, frame_build(TYPE_PING, 1, NULL, 0, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[0]);         /* untouched */
}

static void test_build_rejects_null_output(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, frame_build(TYPE_PING, 1, NULL, 0, NULL, 64));
}

static void test_build_rejects_null_payload_with_length(void)
{
    uint8_t out[PROTO_MAX_FRAME];
    TEST_ASSERT_EQUAL_UINT32(0, frame_build(TYPE_SET_CONFIG, 1, NULL, 4, out, sizeof out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_ping_matches_spec);
    RUN_TEST(test_build_ack_matches_spec);
    RUN_TEST(test_build_nack_with_payload_matches_spec);
    RUN_TEST(test_build_max_payload_128);
    RUN_TEST(test_build_rejects_payload_129);
    RUN_TEST(test_build_rejects_small_output_buffer);
    RUN_TEST(test_build_rejects_null_output);
    RUN_TEST(test_build_rejects_null_payload_with_length);
    return UNITY_END();
}
