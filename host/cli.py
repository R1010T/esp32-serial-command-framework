#!/usr/bin/env python3
"""
cli.py - Laptop side: send commands to the ESP32, print the replies

WHAT:    Builds packets in the same format as components/proto, sends
         them over the USB serial port, and reads the replies back.
WHY:     The other end of the protocol, and the test bench: it measures
         reply time and retries when a reply doesn't come.
ANALOGY: The scan tool. The ESP32 is the engine computer.

Run it in the ESP-IDF terminal (its Python already has pyserial):
    python host/cli.py --port COM4 ping
    python host/cli.py --port COM4 ping --count 100
    python host/cli.py --port COM4 status

Close idf.py monitor first: only one program can hold COM4 at a time.

INTERVIEW LINE:
    "A small Python tool mirrors the C packet format, retries with the same
     sequence number on timeout, and measures round-trip latency."
"""
import argparse
import binascii
import random
import struct
import sys
import time

import serial  # pyserial

# ---- Protocol constants: same values as components/proto/include/frame.h ----
SYNC = b"\xAA\x55"
VERSION = 0x01
MAX_PAYLOAD = 128
HEADER_LEN = 6
CRC_LEN = 2

TYPE_PING = 0x01
TYPE_GET_STATUS = 0x02
TYPE_ACK = 0x80
TYPE_NACK = 0x81
TYPE_STATUS = 0x82

NACK_NAMES = {0x01: "UNKNOWN_TYPE", 0x02: "BAD_VERSION",
              0x03: "BAD_PAYLOAD", 0x04: "BUSY"}

# STATUS payload: 8 numbers, 4 bytes each, low byte first (protocol.md section 10)
STATUS_FIELDS = ("uptime_ms", "frames_ok", "err_len", "err_crc",
                 "err_timeout", "dup", "seq_gap", "nack_sent")

REPLY_TIMEOUT_S = 0.5   # protocol.md section 8
MAX_RETRIES = 3


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE, the same as crc16.c (check value 0x29B1)."""
    return binascii.crc_hqx(data, 0xFFFF)


def build_frame(ptype: int, seq: int, payload: bytes = b"") -> bytes:
    """The same layout as frame_build() in frame.c."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload longer than 128 bytes")
    body = bytes([VERSION, ptype, len(payload), seq]) + payload
    return SYNC + body + struct.pack("<H", crc16(body))   # CRC low byte first


class Reader:
    """Pulls complete, CRC-checked packets out of the incoming bytes.
    Everything else (boot text, noise, damaged packets) is skipped."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf += data

    def next_frame(self):
        """Return (type, seq, payload) for the next good packet, or None for now."""
        while True:
            start = self.buf.find(SYNC)
            if start < 0:
                # No sync yet. Keep a trailing AA: it may be the start of one.
                self.buf = self.buf[-1:] if self.buf[-1:] == b"\xAA" else bytearray()
                return None
            del self.buf[:start]                       # drop junk before the sync
            if len(self.buf) < HEADER_LEN:
                return None                            # header not all here yet
            length = self.buf[4]
            if length > MAX_PAYLOAD:                   # Session 1, Q2: reject, hunt again
                del self.buf[:1]
                continue
            total = HEADER_LEN + length + CRC_LEN
            if len(self.buf) < total:
                return None                            # rest not here yet
            body = bytes(self.buf[2:HEADER_LEN + length])
            (crc_rx,) = struct.unpack("<H", self.buf[HEADER_LEN + length:total])
            if crc_rx != crc16(body):                  # Session 1, Q4: damaged, hunt again
                del self.buf[:1]
                continue
            del self.buf[:total]
            return body[1], body[3], body[4:]          # type, seq, payload


def transact(ser, reader, ptype, seq, payload=b""):
    """Send one command and wait for the reply with the same Seq.
    No reply in 0.5 s: resend the SAME packet (same Seq), up to 3 more times.
    Returns (reply type, reply payload, milliseconds, retries used) or None.
    The time is for the attempt that got the reply."""
    packet = build_frame(ptype, seq, payload)
    for attempt in range(1 + MAX_RETRIES):
        t0 = time.perf_counter()
        ser.write(packet)
        while time.perf_counter() - t0 < REPLY_TIMEOUT_S:
            reader.feed(ser.read(ser.in_waiting or 1))
            while (frame := reader.next_frame()) is not None:
                rtype, rseq, rpayload = frame
                if rseq == seq:
                    return rtype, rpayload, (time.perf_counter() - t0) * 1000.0, attempt
                # A late reply to an earlier packet: ignore it.
    return None


def describe(rtype, payload):
    """Reply type in words."""
    if rtype == TYPE_ACK:
        return "ACK"
    if rtype == TYPE_NACK:
        code = payload[0] if payload else -1
        return "NACK " + NACK_NAMES.get(code, f"code {code}")
    return f"type 0x{rtype:02X} ({len(payload)} bytes)"


def cmd_ping(ser, reader, seq, count):
    times = []
    retries = 0
    for i in range(count):
        s = (seq + i) & 0xFF                     # next number, 255 wraps to 0
        reply = transact(ser, reader, TYPE_PING, s)
        if reply is None:
            print(f"PING seq={s}: no reply after {1 + MAX_RETRIES} tries")
            continue
        rtype, rpayload, ms, tries = reply
        retries += tries
        if rtype == TYPE_ACK:
            times.append(ms)
            if count == 1:
                print(f"PING seq={s} -> ACK in {ms:.1f} ms")
        else:
            print(f"PING seq={s} -> {describe(rtype, rpayload)}")
    if count > 1:
        if times:
            print(f"{len(times)}/{count} ACKs, {retries} retries. Reply time: "
                  f"min {min(times):.1f} ms, avg {sum(times) / len(times):.1f} ms, "
                  f"max {max(times):.1f} ms")
        else:
            print(f"0/{count} ACKs")
    return 0 if len(times) == count else 1


def cmd_status(ser, reader, seq):
    reply = transact(ser, reader, TYPE_GET_STATUS, seq)
    if reply is None:
        print(f"GET_STATUS: no reply after {1 + MAX_RETRIES} tries")
        return 1
    rtype, payload, ms, _tries = reply
    if rtype != TYPE_STATUS or len(payload) != 4 * len(STATUS_FIELDS):
        print(f"GET_STATUS -> {describe(rtype, payload)}")
        return 1
    values = struct.unpack("<8I", payload)
    print(f"STATUS (reply in {ms:.1f} ms)")
    for name, value in zip(STATUS_FIELDS, values):
        print(f"  {name:<12} {value}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Talk to the ESP32 serial command framework.")
    ap.add_argument("--port", required=True, help="serial port, e.g. COM4")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_ping = sub.add_parser("ping", help="check the ESP32 answers")
    p_ping.add_argument("--count", type=int, default=1, help="send N pings, print reply-time stats")
    sub.add_parser("status", help="print the ESP32's counters")
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200          # ignored on the native USB port; needed on a UART
    ser.timeout = 0.01             # a read waits at most 10 ms
    ser.dtr = False                # leave the reset/boot lines alone when opening,
    ser.rts = False                # so the ESP32 doesn't reboot
    try:
        ser.open()
    except serial.SerialException as e:
        print(f"Can't open {args.port}: {e}")
        print("Is idf.py monitor still running? Close it with Ctrl+] first.")
        return 2

    reader = Reader()
    seq = random.randrange(256)    # fresh start, so a new run isn't taken for a repeat
    try:
        if args.cmd == "ping":
            return cmd_ping(ser, reader, seq, args.count)
        return cmd_status(ser, reader, seq)
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
