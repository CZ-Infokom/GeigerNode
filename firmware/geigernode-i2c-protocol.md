# GeigerNode native I2C protocol v1

Slave address `0x67` (provisional). This is GeigerNode's own map, not ClimateGuard RadSens. Hardware TWI0 is the slave on PC4 (SDA) and PC5 (SCL). The RadSens image at `0x66` is `make PROTOCOL=radsens`. The bit-banged image remains on `fallback/bitbang-i2c`.

A master write's first data byte is the register pointer. Further data bytes are acknowledged and ignored. A pointer of `0x1E` or higher is stored as `0x00`. A read with no pointer byte starts at `0x00`. Bytes inside one read increment. After `0x1D`, further bytes in that burst are `0xFF`.

All multi-byte values are unsigned big-endian. Reads do not clear counters. A read latches the published bank on SLA+R so `0x04`..`0x1D` cannot tear. The sample sequence stays put until main publishes a new snapshot, once per second while the tube is in RUN.

Meshtastic only needs `0x04`..`0x0B` (nSv/h, CPM × 10, status). Diagnostic hosts may read through `0x1D`.

HV still starts after 500 ms. Native v1 has no host HV-off register. Stay clear of the HV section. Do not raise 380 V or 2400 Hz. Do not add dead time.

## Map

| Address | Type | Description |
| --- | --- | --- |
| `0x00`..`0x01` | uint16 | Device ID `0x474E` ("GN") |
| `0x02` | uint8 | Protocol version `1` |
| `0x03` | uint8 | Capabilities `0x03` (bit 0 = CPM/nSv/h, bit 1 = raw count + elapsed) |
| `0x04`..`0x07` | uint32 | Estimated dose rate, nSv/h |
| `0x08`..`0x0B` | uint32 | Count rate, CPM × 10 |
| `0x0C`..`0x0D` | uint16 | Measurement and HV status |
| `0x0E`..`0x11` | uint32 | Sample sequence |
| `0x12`..`0x15` | uint32 | Calibration, Q16.16 nSv/h per CPM |
| `0x16`..`0x19` | uint32 | Raw pulse count in the measurement window |
| `0x1A`..`0x1D` | uint32 | Actual observation duration, ms |

## Status `0x0C`..`0x0D`

Low byte:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `VALID` | Snapshot is a real RUN window |
| 1 | `WINDOW_FULL` | 500 s collected |
| 2 | `HV_FOLLOWING` | Converter commanded, including the 500 ms wait, settle, RUN, and cooldown |
| 3 | `HV_SETTLING` | Waiting for 380 V to hold the settle band |
| 4 | `HV_COOLDOWN` | Switch off between retries |
| 5 | `HV_FAULT` | Given up |
| 6 | `OVERFLOW` | Count, elapsed, CPM, dose, or sequence saturated |

High byte: HV attempts already used (0..4), same meaning as RadSens `0x0B` high nibble.

Idle (HV off) and fault clear `VALID` and `HV_FOLLOWING`. The last valid dose, CPM, count, elapsed, and sequence stay in the bank. They are not rewritten as zero radiation.

## Radiation

The sensor keeps a rolling window of about 500 one-second buckets, independent of I2C polling. Until 500 s exist, rates use only the seconds collected so far (`WINDOW_FULL` clear). Each bucket stores the pulses in that interval and the actual millisecond duration of the interval (nominal 1000 ms plus loop latency, clamped at +255 ms). `elapsed_ms` is the sum of those durations.

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

That is not a new empirical fit and is not computed through µR/h at runtime. Do not replace it until a JNA DR-M3 (or other accepted) comparison lands. Calibration is published at `0x12` and is not writable in v1.

90 pulses in 500000 ms must read as CPM × 10 = 108 and 100 nSv/h with this coefficient.

A new RUN still clears the ring. Until the first new one-second snapshot, `VALID` stays clear and the previous frozen bytes remain. Sequence increments only when that new snapshot is published.

Host absence does not enlarge the window. After the ring is full, elapsed stays near 500000 ms. Sequence is a uint32 that saturates rather than wrapping.

## Compatibility

| Image | Build | Address | Map |
| --- | --- | --- | --- |
| Native v1 | `make PROTOCOL=native` | `0x67` | this file |
| RadSens | `make PROTOCOL=radsens` | `0x66` | `i2c-register-map.md` |

One firmware image serves one map. The BME680 on this board stays at `0x76`. Address `0x67` does not collide with it. Long-interval cumulative counters are not part of protocol v1.

## Reads

ESP32-C3 serial bridge (`/mnt/Work/dev/PlatformIO/Projects/I2C_bridge`), 115200 baud:

```text
s
r 67 00 2
r 67 02 1
r 67 04 8
r 67 00 30
r 76 D0 1
```

A scan lists `0x67` and `0x76`. `r 67 00 2` returns `47 4E`. `r 67 02 1` returns `01`. `r 76 D0 1` returns `61`.
