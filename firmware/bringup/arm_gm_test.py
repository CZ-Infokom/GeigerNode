#!/usr/bin/env python3
"""Arm the SI-29BG one-shot. Does not touch flash or fuses.

A valid version-4 record changes the state word at byte 102 and the checksum
at byte 104. An older GM record is replaced. Bytes outside that 106-byte
record are left alone. This file does not invoke the programmer. The caller
sends the printed terminal lines to avrdude.
"""

import struct
import sys

ADDR = 256
FMT = "<4H4I41H"
SIZE = struct.calcsize(FMT)
WORDS = SIZE // 2
MAGIC = 0x474D
VERSION = 4
ARMED = 0xA541
STATE_WORD = 51
CHECKSUM_WORD = 52


def checksum(raw):
    total = 0
    for index in range(0, SIZE - 2, 2):
        total = (total + raw[index] + (raw[index + 1] << 8)) & 0xFFFF
    return total


def pack(words):
    raw = struct.pack("<%uH" % WORDS, *words)
    words = list(words)
    words[CHECKSUM_WORD] = checksum(raw)
    return struct.pack("<%uH" % WORDS, *words)


def fresh():
    words = [0] * WORDS
    words[0] = MAGIC
    words[1] = VERSION
    words[STATE_WORD] = ARMED
    return pack(words)


def load(path):
    data = open(path, "rb").read()
    if len(data) < ADDR + SIZE:
        data = data + bytes([0xFF]) * (ADDR + SIZE - len(data))
    return bytearray(data)


def valid(raw):
    magic, version = struct.unpack_from("<HH", raw, 0)
    return magic == MAGIC and version == VERSION and checksum(raw) == (raw[-2] + (raw[-1] << 8))


def commands(before, after):
    start = None
    lines = []
    for index in range(ADDR, ADDR + SIZE):
        if before[index] == after[index]:
            if start is not None:
                lines.append(format_write(start, after[start:index]))
                start = None
            continue
        if start is None:
            start = index
    if start is not None:
        lines.append(format_write(start, after[start:ADDR + SIZE]))
    return lines


def format_write(address, chunk):
    values = " ".join("0x%02x" % byte for byte in chunk)
    return "write eeprom %u %s" % (address, values)


def arm(image):
    before = bytes(image)
    raw = bytes(image[ADDR:ADDR + SIZE])
    if valid(raw):
        words = list(struct.unpack("<%uH" % WORDS, raw))
        words[STATE_WORD] = ARMED
        updated = pack(words)
        note = "ARMING: changing state and checksum only."
    else:
        updated = fresh()
        note = "ARMING: EEPROM had no valid SI-29BG record. Writing a new ARMED record."
    if not valid(updated) or struct.unpack_from("<H", updated, STATE_WORD * 2)[0] != ARMED:
        raise SystemExit("arm record failed its own checksum")
    after = bytearray(before)
    after[ADDR:ADDR + SIZE] = updated
    # Padding may have extended a short file. Only the record bytes are compared.
    if len(before) < ADDR + SIZE:
        before = before + bytes([0xFF]) * (ADDR + SIZE - len(before))
    return note, commands(before, after)


def self_test():
    if SIZE != 106 or STATE_WORD != 51:
        raise SystemExit("record layout drifted")
    note, lines = arm(bytearray([0xFF]) * 1024)
    if "no valid" not in note or not lines:
        raise SystemExit("blank EEPROM was not armed")
    if not lines[0].startswith("write eeprom 256 "):
        raise SystemExit("arm write did not start at address 256")
    image = bytearray([0xFF]) * 1024
    image[ADDR:ADDR + SIZE] = fresh()
    note, lines = arm(image)
    if lines:
        raise SystemExit("already-armed record produced a write")
    words = list(struct.unpack("<%uH" % WORDS, fresh()))
    words[STATE_WORD] = 0xC763
    words[2] = 380
    done = pack(words)
    image[ADDR:ADDR + SIZE] = done
    note, lines = arm(image)
    if "checksum only" not in note or len(lines) != 1:
        raise SystemExit("re-arm did not keep the old payload")
    if "write eeprom 256" in lines[0]:
        raise SystemExit("re-arm rewrote the whole record")
    print("self-test ok")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
        return 0
    if len(sys.argv) != 3:
        print("usage: arm_gm_test.py EEPROM.bin CMDS.txt", file=sys.stderr)
        return 2
    note, lines = arm(load(sys.argv[1]))
    print(note, file=sys.stderr)
    if not lines:
        print("EEPROM is already ARMED. No bytes to write.", file=sys.stderr)
        open(sys.argv[2], "w").write("")
        return 0
    open(sys.argv[2], "w").write("\n".join(lines) + "\n")
    print("ARMING writes %u EEPROM range(s). Flash and fuses are not modified." % len(lines),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
