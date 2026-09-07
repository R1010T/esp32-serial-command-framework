/*
 * crc16.h - CRC-16/CCITT-FALSE (the damage check at the end of every packet)
 *
 * Parameters (pinned in docs/protocol.md, section 3):
 *   poly 0x1021, init 0xFFFF, no reflection, final XOR 0x0000
 *   check value: CRC of ASCII "123456789" = 0x29B1
 */
#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT 0xFFFFu  /* starting value before any byte is added */
#define CRC16_POLY 0x1021u  /* the "recipe" number: x^16 + x^12 + x^5 + 1 */

/* Add one byte to a running CRC. The parser uses this as bytes arrive. */
uint16_t crc16_update(uint16_t crc, uint8_t byte);

/* CRC of a whole buffer in one call. The packet builder uses this. */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

#endif /* CRC16_H */
