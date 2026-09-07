/*
 * test_crc16.c - Unit tests for crc16.c
 *
 * Runs on the laptop. Proves the CRC matches the standard and the
 * protocol.md examples before any of it runs on the ESP32.
 */
#include "unity.h"
#include "crc16.h"

void setUp(void) {}
void tearDown(void) {}

/* The standard check: every correct CRC-16/CCITT-FALSE gives 0x29B1 here. */
static void test_check_value_123456789(void)
{
    const uint8_t msg[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt_false(msg, sizeof msg));
}

/* No bytes in -> the CRC stays at its starting value. */
static void test_empty_input_returns_init(void)
{
    const uint8_t dummy = 0;
    TEST_ASSERT_EQUAL_HEX16(CRC16_INIT, crc16_ccitt_false(&dummy, 0));
}

/* Byte-at-a-time (parser style) must equal all-at-once (builder style). */
static void test_incremental_matches_bulk(void)
{
    const uint8_t msg[] = { 0x01, 0x81, 0x01, 0x08, 0x01 };
    uint16_t crc = CRC16_INIT;
    for (unsigned i = 0; i < sizeof msg; i++) {
        crc = crc16_update(crc, msg[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(crc16_ccitt_false(msg, sizeof msg), crc);
}

/* protocol.md section 9: the CRC of every worked example. */
static void test_protocol_md_examples(void)
{
    const uint8_t ping[] = { 0x01, 0x01, 0x00, 0x07 };        /* PING, Seq 7          */
    const uint8_t ack[]  = { 0x01, 0x80, 0x00, 0x07 };        /* ACK, Seq 7           */
    const uint8_t nack[] = { 0x01, 0x81, 0x01, 0x08, 0x01 };  /* NACK, Seq 8          */
    const uint8_t flip[] = { 0x01, 0x03, 0x00, 0x07 };        /* PING, 1 bit flipped  */

    TEST_ASSERT_EQUAL_HEX16(0xB5A3, crc16_ccitt_false(ping, sizeof ping));
    TEST_ASSERT_EQUAL_HEX16(0xB9C9, crc16_ccitt_false(ack,  sizeof ack));
    TEST_ASSERT_EQUAL_HEX16(0xBE69, crc16_ccitt_false(nack, sizeof nack));
    TEST_ASSERT_EQUAL_HEX16(0xDBC3, crc16_ccitt_false(flip, sizeof flip));
}

/* Session 1, Q4 as code: flipping ANY single bit must change the CRC. Try all 32. */
static void test_every_single_bit_flip_is_detected(void)
{
    const uint8_t good[] = { 0x01, 0x01, 0x00, 0x07 };
    const uint16_t good_crc = crc16_ccitt_false(good, sizeof good);

    for (unsigned byte_i = 0; byte_i < sizeof good; byte_i++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            uint8_t bad[sizeof good];
            for (unsigned k = 0; k < sizeof good; k++) {
                bad[k] = good[k];
            }
            bad[byte_i] ^= (uint8_t)(1u << bit);   /* flip exactly one bit */
            TEST_ASSERT_NOT_EQUAL(good_crc, crc16_ccitt_false(bad, sizeof bad));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value_123456789);
    RUN_TEST(test_empty_input_returns_init);
    RUN_TEST(test_incremental_matches_bulk);
    RUN_TEST(test_protocol_md_examples);
    RUN_TEST(test_every_single_bit_flip_is_detected);
    return UNITY_END();
}
