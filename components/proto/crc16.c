/*
 * crc16.c - CRC-16/CCITT-FALSE
 *
 * WHAT:    Turns any run of bytes into one 16-bit number. Under the hood
 *          it is long division in binary; the CRC is the remainder.
 * WHY:     The sender appends the number. The receiver recomputes it.
 *          Different number = the packet was damaged on the way.
 * ANALOGY: The check digit on a credit card number. Change one digit
 *          and the check fails.
 *
 * INTERVIEW LINE:
 *   "Bitwise CRC-16/CCITT-FALSE, verified against the standard check
 *    value 0x29B1. No lookup table: 8 shift steps per byte is plenty
 *    at 115200 baud, and it saves 512 bytes."
 */
#include "crc16.h"

uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    /* Put the new byte into the top 8 bits of the CRC. */
    crc ^= (uint16_t)((uint16_t)byte << 8);

    /* Process those 8 bits one at a time, top bit first. */
    for (int bit = 0; bit < 8; bit++) {
        if (crc & 0x8000u) {
            /* Top bit is 1: shift left and mix in the polynomial. */
            crc = (uint16_t)((crc << 1) ^ CRC16_POLY);
        } else {
            /* Top bit is 0: just shift left. */
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < len; i++) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}
