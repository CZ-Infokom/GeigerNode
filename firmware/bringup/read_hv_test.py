#!/usr/bin/env python3
"""Decode the one closed-loop HV EEPROM record."""

import struct
import sys

NAMES = {
    1: "HV_TEST_OK",
    2: "HV_TEST_TIMEOUT",
    3: "HV_TEST_SATURATED",
    4: "HV_TEST_OVERVOLTAGE",
    5: "HV_TEST_FAULT",
}

LABELS = (
    "target_volts",
    "final_adc",
    "max_adc",
    "final_pulse_us",
    "max_pulse_us",
    "final_hv_volts",
    "max_hv_volts",
)


def main() -> int:
    data = open(sys.argv[1], "rb").read(18)
    fields = struct.unpack("<9H", data)
    if fields[0] != 0x4837:
        print("no HV test record, magic 0x%04x" % fields[0])
        return 1
    print("magic 0x%04x" % fields[0])
    for label, value in zip(LABELS, fields[1:]):
        print("%s %u" % (label, value))
    print("status %u %s" % (fields[8], NAMES.get(fields[8], "unknown")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
