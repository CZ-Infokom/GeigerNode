#!/usr/bin/env python3
"""Decode the HV frequency-characterization EEPROM record. Read only."""

import struct
import sys

MAGIC = 0x4653
VERSION = 8
TEST_COUNT = 6
RESULT_WORDS = 14
RECORD_WORDS = 4 + TEST_COUNT * RESULT_WORDS + 1
RECORD_SIZE = RECORD_WORDS * 2

STATE = {
    0xA541: "ARMED",
    0xB652: "RUNNING",
    0xC763: "COMPLETE",
    0xD874: "ABORTED",
}

STATUS = {
    0: "NOT_RUN",
    1: "HV_TEST_OK",
    2: "HV_TEST_TIMEOUT",
    3: "HV_TEST_SATURATED",
    4: "HV_TEST_OVERVOLTAGE",
    5: "HV_TEST_FAULT",
}


def checksum(words):
    return sum(words[:-1]) & 0xFFFF


def show(value, unset):
    if value == 0xFFFF:
        return unset
    return str(value)


def decode(data):
    if len(data) < RECORD_SIZE:
        raise ValueError("EEPROM image is shorter than %u bytes" % RECORD_SIZE)
    words = list(struct.unpack("<%uH" % RECORD_WORDS, data[:RECORD_SIZE]))
    if words[0] != MAGIC or words[1] != VERSION or checksum(words) != words[-1]:
        raise ValueError("no frequency series record, magic 0x%04x" % words[0])
    lines = ["GeigerNode HV frequency characterization", ""]
    for index in range(TEST_COUNT):
        row = words[4 + index * RESULT_WORDS:4 + (index + 1) * RESULT_WORDS]
        lines.extend([
            "Test %u" % index,
            "Target:              %u V" % row[0],
            "Target ADC:          %s" % show(row[1], "n/a"),
            "Frequency:           %u Hz" % row[2],
            "Final ADC:           %s" % show(row[3], "n/a"),
            "Max ADC:             %s" % show(row[4], "n/a"),
            "Min ADC settled:     %s" % show(row[5], "n/a"),
            "Final HV:            %s V" % show(row[6], "n/a"),
            "Max HV:              %s V" % show(row[7], "n/a"),
            "Final pulse:         %s us" % show(row[8], "n/a"),
            "Max pulse:           %s us" % show(row[9], "n/a"),
            "Min pulse settled:   %s us" % show(row[10], "n/a"),
            "Updates at 100 us:   %s" % show(row[11], "n/a"),
            "Time at 100 us:      %s ms" % show(row[12], "n/a"),
            "Status:              %s" % STATUS.get(row[13], "unknown (%u)" % row[13]),
            "",
        ])
    state = STATE.get(words[2], "unknown (0x%04x)" % words[2])
    lines.append("Series:")
    lines.append(state)
    lines.append("Completed tests: %u" % words[3])
    if words[2] == 0xB652:
        lines.append("This read reset the MCU. A RUNNING series latches ABORTED and does not restart.")
    return "\n".join(lines)


def self_test():
    words = [0] * RECORD_WORDS
    words[0] = MAGIC
    words[1] = VERSION
    words[2] = 0xC763
    words[3] = TEST_COUNT
    words[4] = 400
    words[5] = 932
    words[6] = 2500
    words[17] = 1
    words[-1] = checksum(words)
    text = decode(struct.pack("<%uH" % RECORD_WORDS, *words))
    if "Target:              400 V" not in text or "COMPLETE" not in text:
        raise SystemExit("self-test failed")
    if checksum(words) != (sum(words[:-1]) & 0xFFFF):
        raise SystemExit("checksum failed")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
        print("self-test ok")
        return 0
    if len(sys.argv) != 2:
        print("usage: read_freq_series.py EEPROM.bin", file=sys.stderr)
        return 2
    data = open(sys.argv[1], "rb").read()
    try:
        print(decode(data))
    except ValueError as exc:
        print(exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
