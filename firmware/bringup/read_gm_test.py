#!/usr/bin/env python3
"""Decode the SI-29BG EEPROM record. Read only.

Version 1 is the 60 s path test. Version 2 is the 10-minute run with
1 ms cumulative bins. Version 3 is the exclusive 1 us histogram.
Version 4 adds the first 16 gaps below 500 us. Version 4 state is
byte 102 and the checksum is byte 104, matching offsetof() in
firmware/src/gm_test.c.
"""

import struct
import sys

ADDR = 256
FMT_LEGACY = "<4H7I16H"
FMT_V3 = "<4H4I24H"
FMT = "<4H4I41H"
SIZE_LEGACY = struct.calcsize(FMT_LEGACY)
SIZE_V3 = struct.calcsize(FMT_V3)
SIZE = struct.calcsize(FMT)
MAGIC = 0x474D
VERSIONS = {1: 60, 2: 600, 3: 600, 4: 600}

STATE = {
    0xA541: "ARMED",
    0xB652: "RUNNING",
    0xC763: "COMPLETE",
    0xD874: "ABORTED",
}

STATUS = {
    0: "NOT_FINISHED",
    1: "GM_TEST_OK",
    2: "GM_TEST_NO_PULSES",
    3: "GM_TEST_EXCESSIVE_FAST_PULSES",
    4: "GM_TEST_HV_FAULT",
    5: "GM_TEST_HV_TIMEOUT",
    6: "GM_TEST_OVERVOLTAGE",
    7: "GM_TEST_ABORTED",
}


def checksum(raw):
    total = 0
    for index in range(0, len(raw) - 2, 2):
        total = (total + raw[index] + (raw[index + 1] << 8)) & 0xFFFF
    return total


def na(value):
    if value == 0xFFFF:
        return "n/a"
    return str(value)


def line(label, value):
    return "%-23s%s" % (label, value)


def format_cpm(pulses, duration_s):
    scaled = pulses * 60 * 10 // duration_s
    return "%u.%u" % (scaled // 10, scaled % 10)


def header(data, size):
    if len(data) < ADDR + size:
        raise ValueError("EEPROM image is shorter than %u bytes" % (ADDR + size))
    raw = data[ADDR:ADDR + size]
    magic = raw[0] + (raw[1] << 8)
    stored = raw[-2] + (raw[-1] << 8)
    if checksum(raw) != stored or magic != MAGIC:
        raise ValueError("no SI-29BG record, magic 0x%04x" % magic)
    return raw


def common_lines(target, freq, duration_s, window_finished, total, measured, cpm_text,
                 min_cps, max_cps, final_hv, min_hv, max_hv,
                 final_pulse, min_pulse, max_pulse, ms_at_cap,
                 final_adc, min_adc, max_adc, status, state):
    lines = [
        "GeigerNode SI-29BG bring-up",
        "",
        line("HV target:", "%u V" % target),
        line("Frequency:", "%u Hz" % freq),
        "",
        line("Measurement duration:", "%u s" % duration_s if window_finished else "not finished"),
        line("Total GM pulses:", str(total)),
        line("Measurement pulses:", str(measured)),
        line("CPM:", cpm_text),
        "",
        line("Min CPS:", na(min_cps)),
        line("Max CPS:", na(max_cps)),
    ]
    tail = [
        "",
        line("HV final:", "n/a" if final_hv == 0xFFFF else "%u V" % final_hv),
        line("HV minimum:", "n/a" if min_hv == 0xFFFF else "%u V" % min_hv),
        line("HV maximum:", "n/a" if max_hv == 0xFFFF else "%u V" % max_hv),
        "",
        line("Final pulse width:", "n/a" if final_pulse == 0xFFFF else "%u us" % final_pulse),
        line("Minimum pulse width:", "n/a" if min_pulse == 0xFFFF else "%u us" % min_pulse),
        line("Maximum pulse width:", "n/a" if max_pulse == 0xFFFF else "%u us" % max_pulse),
        line("Time at 100 us:", "n/a" if ms_at_cap == 0xFFFF else "%u ms" % ms_at_cap),
        "",
        line("Final ADC:", na(final_adc)),
        line("Min ADC:", na(min_adc)),
        line("Max ADC:", na(max_adc)),
        "",
        line("Status:", STATUS.get(status, "unknown (%u)" % status)),
        line("EEPROM state:", STATE.get(state, "unknown (0x%04x)" % state)),
    ]
    if state == 0xB652:
        tail.append("This read reset the MCU. A RUNNING test latches ABORTED and does not restart.")
    return lines, tail


def decode_legacy(data, version):
    raw = header(data, SIZE_LEGACY)
    fields = struct.unpack(FMT_LEGACY, raw)
    if fields[1] != version or checksum(raw) != fields[-1]:
        raise ValueError("SI-29BG record failed its version or checksum")
    (magic, version, target, freq, total, measured, cpm,
     lt1, lt2, lt5, lt10, min_cps, max_cps, min_interval,
     final_adc, min_adc, max_adc, final_hv, min_hv, max_hv,
     final_pulse, min_pulse, max_pulse, ms_at_cap, status, state,
     _sum) = fields
    if magic != MAGIC:
        raise ValueError("SI-29BG record header does not match")
    duration_s = VERSIONS[version]
    window_finished = cpm != 0xFFFFFFFF
    cpm_text = format_cpm(measured, duration_s) if window_finished else "n/a"
    lines, tail = common_lines(
        target, freq, duration_s, window_finished, total, measured, cpm_text,
        min_cps, max_cps, final_hv, min_hv, max_hv,
        final_pulse, min_pulse, max_pulse, ms_at_cap,
        final_adc, min_adc, max_adc, status, state)
    lines.extend([
        "",
        line("Minimum interval:", "n/a" if min_interval == 0xFFFF else "%u ms" % min_interval),
        line("Intervals <1 ms:", str(lt1)),
        line("Intervals <2 ms:", str(lt2)),
        line("Intervals <5 ms:", str(lt5)),
        line("Intervals <10 ms:", str(lt10)),
        "Interval counts are cumulative. 0 ms means two edges in the same 1 ms tick.",
    ])
    lines.extend(tail)
    return "\n".join(lines)


def interval_lines(min_interval, lt50, b50, b100, b200, b500, b1, b2, b5, bge):
    if min_interval == 0xFFFFFFFF:
        interval_text = "n/a"
    else:
        interval_text = "%u us" % min_interval
    return [
        "",
        line("Minimum interval:", interval_text),
        line("Intervals <50 us:", str(lt50)),
        line("Intervals 50-100 us:", str(b50)),
        line("Intervals 100-200 us:", str(b100)),
        line("Intervals 200-500 us:", str(b200)),
        line("Intervals 500 us-1 ms:", str(b500)),
        line("Intervals 1-2 ms:", str(b1)),
        line("Intervals 2-5 ms:", str(b2)),
        line("Intervals 5-10 ms:", str(b5)),
        line("Intervals >=10 ms:", str(bge)),
        "Each interval is in one bin. 2400 Hz switching is 417 us, in 200-500 us.",
        "A repeatable 100-300 us cluster is the tube or the pulse path.",
    ]


def decode_v3(data):
    raw = header(data, SIZE_V3)
    fields = struct.unpack(FMT_V3, raw)
    if fields[1] != 3 or checksum(raw) != fields[-1]:
        raise ValueError("SI-29BG record failed its version or checksum")
    (magic, version, target, freq, total, measured, cpm, min_interval,
     lt50, b50, b100, b200, b500, b1, b2, b5, bge,
     min_cps, max_cps,
     final_adc, min_adc, max_adc, final_hv, min_hv, max_hv,
     final_pulse, min_pulse, max_pulse, ms_at_cap, status, state,
     _sum) = fields
    if magic != MAGIC or version != 3:
        raise ValueError("SI-29BG record header does not match")
    duration_s = VERSIONS[version]
    window_finished = cpm != 0xFFFFFFFF
    cpm_text = format_cpm(measured, duration_s) if window_finished else "n/a"
    lines, tail = common_lines(
        target, freq, duration_s, window_finished, total, measured, cpm_text,
        min_cps, max_cps, final_hv, min_hv, max_hv,
        final_pulse, min_pulse, max_pulse, ms_at_cap,
        final_adc, min_adc, max_adc, status, state)
    lines.extend(interval_lines(min_interval, lt50, b50, b100, b200, b500, b1, b2, b5, bge))
    lines.extend(tail)
    return "\n".join(lines)


def decode_v4(data):
    raw = header(data, SIZE)
    fields = struct.unpack(FMT, raw)
    if fields[1] != 4 or checksum(raw) != fields[-1]:
        raise ValueError("SI-29BG record failed its version or checksum")
    (magic, version, target, freq, total, measured, cpm, min_interval,
     lt50, b50, b100, b200, b500, b1, b2, b5, bge,
     *rest) = fields
    close = rest[:16]
    (close_count, min_cps, max_cps,
     final_adc, min_adc, max_adc, final_hv, min_hv, max_hv,
     final_pulse, min_pulse, max_pulse, ms_at_cap, status, state,
     _sum) = rest[16:]
    if magic != MAGIC or version != 4:
        raise ValueError("SI-29BG record header does not match")
    if close_count > 16:
        raise ValueError("close-interval count is out of range")
    duration_s = VERSIONS[version]
    window_finished = cpm != 0xFFFFFFFF
    cpm_text = format_cpm(measured, duration_s) if window_finished else "n/a"
    lines, tail = common_lines(
        target, freq, duration_s, window_finished, total, measured, cpm_text,
        min_cps, max_cps, final_hv, min_hv, max_hv,
        final_pulse, min_pulse, max_pulse, ms_at_cap,
        final_adc, min_adc, max_adc, status, state)
    lines.extend(interval_lines(min_interval, lt50, b50, b100, b200, b500, b1, b2, b5, bge))
    seen = lt50 + b50 + b100 + b200
    lines.append("")
    lines.append(line("Close gaps stored:", "%u of %u below 500 us" % (close_count, seen)))
    if close_count == 0:
        lines.append("No interval below 500 us was stored.")
    else:
        lines.append("First gaps below 500 us, in arrival order. They were counted.")
        for index in range(close_count):
            lines.append(line("Close %u:" % (index + 1), "%u us" % close[index]))
    if seen > close_count:
        lines.append("The log kept the first %u. Later close gaps remain in the bins." % close_count)
    lines.append("Blanking is 0 us. No edge was rejected.")
    lines.extend(tail)
    return "\n".join(lines)


def decode(data):
    if len(data) < ADDR + 4:
        raise ValueError("EEPROM image is shorter than %u bytes" % (ADDR + 4))
    version = data[ADDR + 2] + (data[ADDR + 3] << 8)
    if version in (1, 2):
        return decode_legacy(data, version)
    if version == 3:
        return decode_v3(data)
    if version == 4:
        return decode_v4(data)
    magic = data[ADDR] + (data[ADDR + 1] << 8)
    raise ValueError("no SI-29BG record, magic 0x%04x" % magic)


def finish(fields, fmt):
    raw = struct.pack(fmt, *fields)
    total = checksum(raw)
    return raw[:-2] + struct.pack("<H", total)


def self_test():
    if SIZE != 106 or SIZE_V3 != 72 or SIZE_LEGACY != 68:
        raise SystemExit("record size drifted")
    fields = [0] * 49
    fields[0] = MAGIC
    fields[1] = 4
    fields[2] = 380
    fields[3] = 2400
    fields[4] = 85
    fields[5] = 85
    fields[6] = 8
    fields[7] = 151
    fields[10] = 6
    fields[16] = 78
    fields[17] = 151
    fields[18] = 154
    fields[19] = 168
    fields[33] = 3
    fields[34] = 0
    fields[35] = 2
    fields[36] = 887
    fields[37] = 879
    fields[38] = 888
    fields[39] = 380
    fields[40] = 377
    fields[41] = 381
    fields[42] = 80
    fields[43] = 79
    fields[44] = 80
    fields[45] = 0
    fields[46] = 1
    fields[47] = 0xC763
    raw = finish(fields, FMT)
    if raw[102] != 0x63 or raw[103] != 0xC7:
        raise SystemExit("state is not at byte 102")
    text = decode(bytes([0xFF]) * ADDR + raw + bytes([0xFF]) * 16)
    for expected in (
        "Measurement pulses:    85",
        "CPM:                   8.5",
        "Minimum interval:      151 us",
        "Intervals 100-200 us:  6",
        "Close gaps stored:     3 of 6 below 500 us",
        "Close 1:               151 us",
        "Close 2:               154 us",
        "Close 3:               168 us",
        "The log kept the first 3. Later close gaps remain in the bins.",
        "Blanking is 0 us. No edge was rejected.",
        "EEPROM state:          COMPLETE",
    ):
        if expected not in text:
            raise SystemExit("missing %r" % expected)
    fields = [0] * 32
    fields[0] = MAGIC
    fields[1] = 3
    fields[2] = 380
    fields[3] = 2400
    fields[4] = 80
    fields[5] = 73
    fields[6] = 7
    fields[7] = 180  # minimum interval, microseconds
    fields[10] = 2   # 100-200 us
    fields[11] = 1   # 200-500 us, where 417 us switching would land
    fields[16] = 70  # >= 10 ms
    fields[17] = 0
    fields[18] = 2
    fields[19] = 885
    fields[20] = 881
    fields[21] = 886
    fields[22] = 379
    fields[23] = 378
    fields[24] = 380
    fields[25] = 80
    fields[26] = 80
    fields[27] = 80
    fields[28] = 0
    fields[29] = 1
    fields[30] = 0xC763
    raw = finish(fields, FMT_V3)
    if raw[68] != 0x63 or raw[69] != 0xC7:
        raise SystemExit("state is not at byte 68")
    image = bytes([0xFF]) * ADDR + raw + bytes([0xFF]) * 16
    text = decode(image)
    for expected in (
        "HV target:             380 V",
        "Frequency:             2400 Hz",
        "Measurement duration:  600 s",
        "Total GM pulses:       80",
        "Measurement pulses:    73",
        "CPM:                   7.3",
        "Minimum interval:      180 us",
        "Intervals <50 us:      0",
        "Intervals 100-200 us:  2",
        "Intervals 200-500 us:  1",
        "Intervals >=10 ms:     70",
        "HV final:              379 V",
        "Final pulse width:     80 us",
        "Time at 100 us:        0 ms",
        "Status:                GM_TEST_OK",
        "EEPROM state:          COMPLETE",
    ):
        if expected not in text:
            raise SystemExit("missing %r" % expected)
    legacy = [0] * 27
    legacy[0] = MAGIC
    legacy[1] = 2
    legacy[2] = 380
    legacy[3] = 2400
    legacy[4] = 112
    legacy[5] = 112
    legacy[6] = 11
    legacy[7] = 6
    legacy[8] = 8
    legacy[9] = 8
    legacy[10] = 8
    legacy[13] = 0
    legacy[24] = 1
    legacy[25] = 0xC763
    old = finish(legacy, FMT_LEGACY)
    old_text = decode(bytes([0xFF]) * ADDR + old)
    if "Minimum interval:      0 ms" not in old_text or "Intervals <1 ms:       6" not in old_text:
        raise SystemExit("version 2 record did not decode")
    try:
        decode(bytes([0xFF]) * (ADDR + SIZE))
    except ValueError:
        pass
    else:
        raise SystemExit("blank image was accepted")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
        print("self-test ok")
        return 0
    if len(sys.argv) != 2:
        print("usage: read_gm_test.py EEPROM.bin", file=sys.stderr)
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
