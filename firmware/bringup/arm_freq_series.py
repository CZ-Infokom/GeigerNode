#!/usr/bin/env python3
"""Write only the frequency-series arm state. Does not touch flash or fuses.

A valid record changes series_state and the checksum word.
A blank or foreign EEPROM image is replaced by a new ARMED record.
The caller must send the printed terminal lines to avrdude. This file does
not invoke the programmer.
"""

import struct
import sys

MAGIC = 0x4653
VERSION = 8
ARMED = 0xA541
TEST_COUNT = 6
RESULT_WORDS = 14
RECORD_WORDS = 4 + TEST_COUNT * RESULT_WORDS + 1
RECORD_SIZE = RECORD_WORDS * 2


def checksum(words):
    return sum(words[:-1]) & 0xFFFF


def fresh():
    words = [0] * RECORD_WORDS
    words[0] = MAGIC
    words[1] = VERSION
    words[2] = ARMED
    words[-1] = checksum(words)
    return words


def load(path):
    data = open(path, "rb").read()
    if len(data) < RECORD_SIZE:
        data = data + bytes([0xFF]) * (RECORD_SIZE - len(data))
    return list(struct.unpack("<%uH" % RECORD_WORDS, data[:RECORD_SIZE]))


def commands(before, after):
    raw_before = struct.pack("<%uH" % RECORD_WORDS, *before)
    raw_after = struct.pack("<%uH" % RECORD_WORDS, *after)
    start = None
    lines = []
    for index, (old, new) in enumerate(zip(raw_before, raw_after)):
        if old == new:
            if start is not None:
                chunk = raw_after[start:index]
                lines.append(format_write(start, chunk))
                start = None
            continue
        if start is None:
            start = index
    if start is not None:
        lines.append(format_write(start, raw_after[start:]))
    return lines


def format_write(address, chunk):
    values = " ".join("0x%02x" % byte for byte in chunk)
    return "write eeprom %u %s" % (address, values)


def main():
    if len(sys.argv) != 3:
        print("usage: arm_freq_series.py EEPROM.bin CMDS.txt", file=sys.stderr)
        return 2
    before = load(sys.argv[1])
    valid = before[0] == MAGIC and before[1] == VERSION and checksum(before) == before[-1]
    if valid:
        after = list(before)
        after[2] = ARMED
        after[-1] = checksum(after)
        print("ARMING: changing series_state and checksum only.", file=sys.stderr)
    else:
        after = fresh()
        print("ARMING: EEPROM had no valid series record. Writing a new ARMED record.",
              file=sys.stderr)
    if checksum(after) != (sum(after[:-1]) & 0xFFFF) or after[2] != ARMED:
        print("arm record failed its own checksum", file=sys.stderr)
        return 1
    lines = commands(before, after)
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
