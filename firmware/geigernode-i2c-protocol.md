# GeigerNode native I2C protocol v2

Slave address `0x67` (provisional). This is GeigerNode's own map, not ClimateGuard RadSens. Hardware TWI0 is the slave on PC4 (SDA) and PC5 (SCL). The RadSens image at `0x66` is `make PROTOCOL=radsens`. The bit-banged image remains on `fallback/bitbang-i2c`.

A master write's first data byte is the register pointer. The next bytes are the value. A pointer of `0x22` or higher is stored as `0x00`. A read with no pointer byte starts at `0x00`. Bytes inside one read increment. After `0x21`, further bytes in that burst are `0xFF`.

All multi-byte values are unsigned big-endian. Reads do not clear counters. A read latches the published bank on SLA+R so `0x04`..`0x1D` cannot tear. The sample sequence stays put until main publishes a new snapshot, once per second while the tube is in RUN.

Meshtastic only needs `0x04`..`0x0B` (nSv/h, CPM × 10, status). Diagnostic hosts may read through `0x21`.

HV still starts after 500 ms. Native v2 has no host HV-off register. Stay clear of the HV section. Do not raise 380 V or 2400 Hz. An accepted falling edge holds off further edges for the holdoff at `0x20`. An ignored edge does not move that window. The default holdoff is 300 µs.

## Map

| Address | Type | Description |
| --- | --- | --- |
| `0x00`..`0x01` | uint16 | Device ID `0x474E` ("GN") |
| `0x02` | uint8 | Protocol version `2` |
| `0x03` | uint8 | Capabilities `0x07` (bit 0 = CPM/nSv/h, bit 1 = raw count + elapsed, bit 2 = writable window, holdoff, and calibration) |
| `0x04`..`0x07` | uint32 | Estimated dose rate, nSv/h |
| `0x08`..`0x0B` | uint32 | Count rate, CPM × 10 |
| `0x0C`..`0x0D` | uint16 | Measurement and HV status |
| `0x0E`..`0x11` | uint32 | Sample sequence |
| `0x12`..`0x15` | uint32 | Calibration, Q16.16 nSv/h per CPM |
| `0x16`..`0x19` | uint32 | Raw pulse count in the measurement window |
| `0x1A`..`0x1D` | uint32 | Actual observation duration, ms |
| `0x1E`..`0x1F` | uint16 | Static window, seconds. Read/write. Range 1..400. Default 300 |
| `0x20`..`0x21` | uint16 | Holdoff, microseconds. Read/write. Range 0..300. Default 300 |

## Status `0x0C`..`0x0D`

Low byte:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `VALID` | Snapshot is a real RUN window |
| 1 | `WINDOW_FULL` | Configured window collected. Default 300 s |
| 2 | `HV_FOLLOWING` | Converter commanded, including the 500 ms wait, settle, RUN, and cooldown |
| 3 | `HV_SETTLING` | Waiting for 380 V to hold the settle band |
| 4 | `HV_COOLDOWN` | Switch off between retries |
| 5 | `HV_FAULT` | Given up |
| 6 | `OVERFLOW` | Count, elapsed, CPM, dose, or sequence saturated |

High byte: HV attempts already used (0..4), same meaning as RadSens `0x0B` high nibble.

Idle (HV off) and fault clear `VALID` and `HV_FOLLOWING`. The last valid dose, CPM, count, elapsed, and sequence stay in the bank. They are not rewritten as zero radiation.

## Radiation

The sensor keeps a history of up to 400 one-second buckets, independent of I2C polling. The published rate uses the newest configured window, default 300 s. Until that many seconds exist, rates use only the seconds collected so far (`WINDOW_FULL` clear). Each bucket stores the pulses in that interval and the actual millisecond duration of the interval (nominal 1000 ms plus loop latency, clamped at +255 ms). `elapsed_ms` is the sum of the durations inside the published window.

```text
cpm_x10 = round(count * 600000 / elapsed_ms)
dose_nsvh = round(count * 60000 * cal_q16 / (elapsed_ms * 65536))
```

CPM and dose describe the same observation. Arithmetic uses 64-bit intermediates and saturates at `UINT32_MAX` with `OVERFLOW` set. `elapsed_ms == 0` yields 0 and does not set `OVERFLOW`.

The default calibration is the SBM-20 Cs-137 datasheet midpoint of 65 impulses/µR expressed directly as nSv/h per CPM:

```text
10 nSv/µR * 60 / 65 = 600/65
Q16.16 = round(600/65 * 65536) = 604948 (0x00093B14)
```

That is not a new empirical fit and is not computed through µR/h at runtime. Do not replace the default until a JNA DR-M3 (or other accepted) comparison lands. A host may override it. The override is not a new accepted coefficient.

90 pulses in 500000 ms must read as CPM × 10 = 108 and 100 nSv/h with this coefficient.

A new RUN still clears the ring. Until the first new one-second snapshot, `VALID` stays clear and the previous frozen bytes remain. Sequence increments only when that new snapshot is published.

Host absence does not enlarge the window. After the configured window is full, elapsed stays near `window_s × 1000` ms. At the default of 300 s that is near 300000 ms. Sequence is a uint32 that saturates rather than wrapping.

## Writes

A write is the pointer byte plus the value bytes. Short writes and out-of-range values are ignored. The previous setting stays.

| Pointer | Length | Value | Effect |
| --- | --- | --- | --- |
| `0x12` | 4 | non-zero Q16.16, big-endian | calibration. Zero is ignored |
| `0x1E` | 2 | seconds, 1..400, big-endian | static window |
| `0x20` | 2 | microseconds, 0..300, big-endian | holdoff |

The window and holdoff registers update when the write is accepted. Dose, CPM, count, and elapsed use the new window on the next one-second snapshot. Shrinking drops the oldest seconds. Lengthening keeps seconds still in the 400 s history and leaves `WINDOW_FULL` clear until the new length is filled. Holdoff applies to the next edge. Zero holdoff counts every falling edge.

Accepted settings are stored in EEPROM with magic `0xC3`. A blank or bad record loads the defaults. `make flash` chip-erases EEPROM, so a freshly flashed board returns to 300 s, 300 µs, and `604948`. The RadSens image uses the same EEPROM address with a different magic and does not read this record.

## Compatibility

| Image | Build | Address | Map |
| --- | --- | --- | --- |
| Native v2 | `make PROTOCOL=native` | `0x67` | this file |
| RadSens | `make PROTOCOL=radsens` | `0x66` | `i2c-register-map.md` |

One firmware image serves one map. The BME680 on this board stays at `0x76`. Address `0x67` does not collide with it. Long-interval cumulative counters are not part of protocol v2. The RadSens static window stays 300 s and its holdoff stays 300 µs. Its calibration is the sensitivity register at `0x12` on that map.

## Reads

ESP32-C3 serial bridge (`/mnt/Work/dev/PlatformIO/Projects/I2C_bridge`), 115200 baud:

```text
s
r 67 00 2
r 67 02 1
r 67 04 8
r 67 00 34
r 76 D0 1
```

A scan lists `0x67` and `0x76`. `r 67 00 2` returns `47 4E`. `r 67 02 1` returns `02`. `r 76 D0 1` returns `61`. Writing `00 09 3B 14` at `0x12` stores the default calibration.
