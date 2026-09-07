# Serial Protocol v1: ESP32 Command Framework

Plain version: the laptop and the ESP32 talk in fixed-shape packets. Every packet has a start marker, a small header, the data, and a damage check.

## 1. Link
- UART, 115200 baud, 8N1 (8 data bits, no parity, 1 stop bit).
- Which USB port carries it (COM4 or COM5) is decided at file 8.
- One command in flight at a time. The laptop waits for the reply, or a timeout, before it sends the next command.

## 2. Packet layout
| Byte(s) | Field | Size | Meaning |
|---|---|---|---|
| 0–1 | Sync | 2 | Always `AA 55`. Start marker. |
| 2 | Version | 1 | Format version. v1 = `0x01`. |
| 3 | Type | 1 | What the packet is (section 4). |
| 4 | Length | 1 | Number of payload bytes that follow. 0–128. |
| 5 | Seq | 1 | Message number, 0–255, wraps back to 0. |
| 6 … 5+Length | Payload | 0–128 | Data. Its meaning depends on Type. |
| last 2 | CRC16 | 2 | Damage check. Low byte first. |

- Header = 6 bytes. Smallest packet (no payload) = 8 bytes. Largest = 6 + 128 + 2 = 136 bytes.
- 136 bytes at 115200 baud ≈ 11.8 ms on the wire.

## 3. Decisions (the 4 open items, now closed)
| Item | Decision | Why |
|---|---|---|
| Max payload | 128 bytes (`MAX_PAYLOAD`) | Fixed buffer, no malloc. Any Length above 128 is rejected before a single byte is stored. |
| CRC covers | Version, Type, Length, Seq, Payload | Everything after sync. Sync never changes, so checking it adds nothing. |
| CRC byte order | Low byte first (little-endian) | The ESP32 and the laptop are both little-endian. Same order on both sides. |
| CRC variant | CRC-16/CCITT-FALSE | Parameters below. The name "CRC16-CCITT" alone is ambiguous: XMODEM and KERMIT use the same polynomial and give different results. |

CRC-16/CCITT-FALSE parameters:
| Parameter | Value |
|---|---|
| Width | 16 bits |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Reflect in / out | No / No |
| Final XOR | `0x0000` |
| Check: CRC of ASCII `"123456789"` | `0x29B1` |

- The check value is the first unit test (file 3). A wrong variant gives a wrong check value.
- Laptop side: Python's `binascii.crc_hqx(data, 0xFFFF)` computes the same CRC. No extra package needed.

## 4. Types
Commands (laptop → ESP32) have the high bit clear (`0x01`–`0x7F`).
| Type | Name | Payload | Reply |
|---|---|---|---|
| `0x01` | PING | none | ACK |
| `0x02` | GET_STATUS | none | STATUS |
| `0x03` | GET_CONFIG | none | ACK + config bytes |
| `0x04` | SET_CONFIG | config bytes | ACK or NACK |
| `0x05` | START_STREAM | TBD | ACK, then TELEMETRY packets |
| `0x06` | STOP_STREAM | none | ACK |
| `0x07` | INJECT_FAULT | fault code (TBD) | ACK |
| `0x08` | CLEAR_LOG | none | ACK |

Responses (ESP32 → laptop) have the high bit set (`0x80`–`0xFF`).
| Type | Name | Payload |
|---|---|---|
| `0x80` | ACK | optional data |
| `0x81` | NACK | 1 byte: error code (section 5) |
| `0x82` | STATUS | TBD (uptime, error counters) |
| `0x83` | TELEMETRY | TBD |

- A reply carries the **same Seq** as the command it answers.
- TELEMETRY packets use the ESP32's own counter in Seq.
- Payloads marked TBD get defined in the file that needs them.

## 5. NACK error codes
| Code | Name | When |
|---|---|---|
| `0x01` | UNKNOWN_TYPE | Packet passed CRC, but its Type is not in the table. |
| `0x02` | BAD_VERSION | Packet passed CRC, but Version ≠ `0x01`. |
| `0x03` | BAD_PAYLOAD | Packet passed CRC, but the payload size or content is wrong for that Type. |
| `0x04` | BUSY | The ESP32 can't run the command right now. |

A NACK goes out only for packets that **passed CRC**. A packet that failed CRC can't be trusted; even its Seq may be wrong. So it gets no reply, and the laptop's timeout handles it.

## 6. Receive rules (the ESP32 parser, file 5)
1. When idle, drop bytes until `AA` arrives, then expect `55`.
   - `AA AA 55`: the second `AA` restarts the match. It is still a valid start.
2. Read the 4 header bytes: Version, Type, Length, Seq.
3. Length > 128 → reject at once, count `err_len`, go back to step 1. Nothing gets stored.
4. Read Length payload bytes into the fixed 128-byte buffer.
5. Read the 2 CRC bytes. Recompute the CRC over bytes 2 … 5+Length.
   - Mismatch → drop the packet, count `err_crc`, go back to step 1. No reply.
6. Inter-byte timeout: a gap of more than 50 ms in the middle of a packet → drop the partial packet, count `err_timeout`, go back to step 1. (50 ms is far longer than one full packet, 11.8 ms.)
7. The payload may contain `AA 55`. That's fine: the parser hunts for sync only when idle. A false start fails the CRC check at step 5.

## 7. Sequence rules
1. Laptop: a new command gets Seq + 1 (255 wraps to 0). A retry resends the same packet with the same Seq.
2. The ESP32 stores the last Seq, the last CRC, and the last reply it sent.
3. Same Seq **and** same CRC as the last packet → duplicate. Don't run it again. Resend the stored reply. Count `dup`.
   - Why check CRC too: if the laptop restarts and reuses an old Seq for a *different* command, the CRC differs. The command is treated as new instead of being wrongly skipped.
4. Seq is not last + 1 and the packet is not a duplicate → run it, count `seq_gap` (commands were lost on the way).
5. After boot there is no last Seq, so the first command is accepted with any Seq.

## 8. Timing (laptop)
- Wait 500 ms for a reply. That covers SET_CONFIG, which writes to flash.
- No reply → resend the same packet, same Seq. Maximum 3 retries.
- Still no reply → report a link error.

## 9. Worked examples (real bytes)
CRC is computed over the bytes after sync. Each example shows the CRC value, then the 2 bytes as sent (low byte first).

**PING, Seq 7**
`AA 55 01 01 00 07 A3 B5`
CRC over `01 01 00 07` = `0xB5A3` → sent as `A3 B5`.

**ACK reply, Seq 7**
`AA 55 01 80 00 07 C9 B9`
CRC = `0xB9C9`.

**Unknown Type `0x09`, Seq 8**
`AA 55 01 09 00 08 ED ED`
CRC passes, Type unknown → the ESP32 replies with a NACK:
`AA 55 01 81 01 08 01 69 BE`
Length 1, payload `01` = UNKNOWN_TYPE, CRC = `0xBE69`.

**Bit flip (the Q4 case; here the flipped bit is in the Type byte)**
1. The laptop sends the PING above.
2. Noise flips one bit in Type: `01` → `03`.
3. The ESP32 receives `AA 55 01 03 00 07 A3 B5`.
4. It recomputes the CRC over `01 03 00 07` = `0xDBC3`. The received CRC is `0xB5A3`.
5. Mismatch → drop, count `err_crc`, no reply.
6. The laptop times out after 500 ms and resends the same packet with Seq 7.

Without the CRC, the ESP32 would have run `0x03` = GET_CONFIG instead of PING.

## 10. Error counters (reported in STATUS)
`err_len` · `err_crc` · `err_timeout` · `dup` · `seq_gap` · `nack_sent`

## 11. Known limits of v1
- When the ESP32 reboots it forgets the last Seq. A retry that arrives right after a reboot can run a command twice.
- Only one command can be in flight at a time.
- No authentication: anyone with the cable can send commands.
- No byte stuffing. It isn't needed, because Length plus CRC handle false starts.
