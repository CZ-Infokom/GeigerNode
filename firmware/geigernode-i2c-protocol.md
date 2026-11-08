# GeigerNode native I2C protocol v2

Slave address `0x67` (provisional). This is GeigerNode's own map, not ClimateGuard RadSens. Hardware TWI0 is the slave on PC4 (SDA) and PC5 (SCL). The RadSens image at `0x66` is `make PROTOCOL=radsens`. The bit-banged image remains on `fallback/bitbang-i2c`.

A master write's first data byte is the register pointer. The next bytes are the value. A pointer of `0x50` or higher is stored as `0x00`. A read with no pointer byte starts at `0x00`. Bytes inside one read increment. After `0x4F`, further bytes in that burst are `0xFF`.

All multi-byte values are unsigned big-endian. Reads do not clear counters, except a full read of either latched peak register. A read latches the published bank on SLA+R so `0x04`..`0x4F` cannot tear. The sample sequence stays put until main publishes a new snapshot, once per second while the tube is in RUN.

Meshtastic only needs `0x04`..`0x0B` (nSv/h, CPM × 10, status). Diagnostic hosts may read through `0x4F`. A 34-byte read through `0x21` remains valid. Bytes `0x00`..`0x3C` do not move.

HV still starts after 500 ms. Native v2 has no host HV-off register. Stay clear of the HV section. Do not raise 380 V or 2400 Hz. An accepted falling edge holds off further edges for the holdoff at `0x20`. An ignored edge does not move that window. The default holdoff is 200 µs.

## Map

| Address | Type | Description |
| --- | --- | --- |
| `0x00`..`0x01` | uint16 | Device ID `0x474E` ("GN") |
| `0x02` | uint8 | Protocol version `2` |
| `0x03` | uint8 | Capabilities `0x1F` (bit 0 = CPM/nSv/h, bit 1 = raw count + elapsed, bit 2 = writable window, holdoff, calibration, histogram clear, and peak span, bit 3 = inter-arrival histogram, bit 4 = short-slice peaks) |
| `0x04`..`0x07` | uint32 | Estimated dose rate, nSv/h |
| `0x08`..`0x0B` | uint32 | Count rate, CPM × 10 |
| `0x0C`..`0x0D` | uint16 | Measurement and HV status |
| `0x0E`..`0x11` | uint32 | Sample sequence |
| `0x12`..`0x15` | uint32 | Calibration, Q16.16 nSv/h per CPM |
| `0x16`..`0x19` | uint32 | Raw pulse count in the measurement window |
| `0x1A`..`0x1D` | uint32 | Milliseconds since this RUN started publishing samples. It does not stop when the window fills |
| `0x1E`..`0x1F` | uint16 | Static window, seconds. Read/write. Range 1..400. Default 300 |
| `0x20`..`0x21` | uint16 | Holdoff, microseconds. Read/write. Range 0..300. Default 200 |
| `0x22`..`0x25` | uint32 | Shortest accepted gap this RUN, µs. `0xFFFFFFFF` until the second accepted edge |
| `0x26`..`0x37` | 9 × uint16 | Exclusive inter-arrival bins. Holdoff 0 is under 50 µs, 50–100, 100–200, 200–500, 500 µs–1 ms, 1–2 ms, 2–5 ms, 5–10 ms, 10 ms and above. Any other holdoff starts bin 0 at that holdoff and stretches those eight upper bounds onto `[holdoff, 10000]` µs. Last bin stays 10 ms and above |
| `0x38` | uint8 | Histogram flags. Bit 0 set when any bin is at 65535. Write `01` to clear |
| `0x39` | uint8 | Firmware major. `0` for `0.2.1` |
| `0x3A` | uint8 | Firmware minor. `2` |
| `0x3B` | uint8 | Firmware patch. `1` |
| `0x3C` | uint8 | Pre-release. `0` is a release. This image is `0` |
| `0x3D` | uint8 | Reserved. Reads `0`. Writes are ignored |
| `0x3E`..`0x3F` | uint16 | Peak span, seconds. Read/write. Range 10..60. Default 10 |
| `0x40`..`0x43` | uint32 | Window maximum, CPM × 10. Busiest full peak-span slice still inside the dose window |
| `0x44`..`0x47` | uint32 | Latched maximum, CPM × 10. Busiest full peak-span slice since the previous consume. A full read of this register or of `0x4C`..`0x4F` resets both latched values |
| `0x48`..`0x4B` | uint32 | Window maximum, nSv/h. Same slice as `0x40`, live calibration |
| `0x4C`..`0x4F` | uint32 | Latched maximum, nSv/h. Same slice as `0x44`, live calibration. A full read of this register or of `0x44`..`0x47` resets both latched values |

Writes into `0x22`..`0x37`, `0x39`..`0x3D`, and `0x40`..`0x4F` are acknowledged and ignored. A 1-byte write of `01` at `0x38` clears the histogram. Other values and a short write at `0x38` are ignored. The firmware bytes are the image, not the protocol byte at `0x02`. An older image has no bytes past `0x3C`, so a burst that starts at `0x20` reads `0xFF` there after the old map ends.

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

Idle (HV off) and fault clear `VALID` and `HV_FOLLOWING`. The last valid dose, CPM, count, elapsed, sequence, and peaks stay in the bank. They are not rewritten as zero radiation. A saturated histogram bin does not set `OVERFLOW`.

## Radiation

The sensor keeps a history of up to 400 one-second buckets, independent of I2C polling. The published rate uses the newest configured window, default 300 s. Until that many seconds exist, rates use only the seconds collected so far (`WINDOW_FULL` clear). Each bucket stores the pulses in that interval and the actual millisecond duration of the interval (nominal 1000 ms plus loop latency, clamped at +255 ms). The rate uses the sum of those durations inside the window. That sum stays near `window_s × 1000` ms after the window fills. Register `0x1A` is not that sum. It is the time since this RUN started publishing, and it keeps counting past 300000 ms. A new RUN starts it at 0. It saturates at `0xFFFFFFFF` and sets `OVERFLOW`.

```text
cpm_x10 = round(window_count * 600000 / window_elapsed_ms)
dose_nsvh = round(window_count * 60000 * cal_q16 / (window_elapsed_ms * 65536))
```

`window_count` is register `0x16`. `window_elapsed_ms` stays on the device and is the window sum above. CPM and dose describe that window. Arithmetic uses 64-bit intermediates and saturates at `UINT32_MAX` with `OVERFLOW` set. A zero window duration yields 0 and does not set `OVERFLOW`.

The default calibration is the SBM-20 Cs-137 datasheet midpoint of 65 impulses/µR expressed directly as nSv/h per CPM:

```text
10 nSv/µR * 60 / 65 = 600/65
Q16.16 = round(600/65 * 65536) = 604948 (0x00093B14)
```

That is not a new empirical fit and is not computed through µR/h at runtime. Do not replace the default until a JNA DR-M3 (or other accepted) comparison lands. A host may override it. The override is not a new accepted coefficient.

90 pulses in 500000 ms must read as CPM × 10 = 108 and 100 nSv/h with this coefficient.

A new RUN still clears the ring. Until the first new one-second snapshot, `VALID` stays clear and the previous frozen bytes remain. Sequence increments only when that new snapshot is published. The window peak returns to 0 until a new full peak-span slice exists. The latched peak is not cleared by a new RUN.

Host absence does not enlarge the window. After the configured window is full, the rate still uses about `window_s × 1000` ms, while `0x1A` continues. Sequence is a uint32 that saturates rather than wrapping.

## Short-slice peaks

Background is about 30 CPM, so a one-second maximum is mostly counting noise. The peak span at `0x3E` is a host-set slice of consecutive one-second buckets, range 10..60 s, default 10. At about 30 CPM a 10 s slice holds about 5 counts. The dose average at `0x04`/`0x08` still uses the longer window at `0x1E`.

A slice uses the sum of those bucket counts and the sum of their durations. The same CPM and dose formulas apply. Ranking uses `count_a * ms_b` versus `count_b * ms_a`. On a tie, the newer slice wins. Dose is recomputed from the stored count and duration with the live calibration, so a later calibration write updates the nSv/h form on the next publish and leaves the CPM peak where it is.

No peak is published until a full slice exists. If the dose window is shorter than the span, both peaks read 0. Defaults are 300 s and 10 s.

The window maximum is the busiest full slice that lies entirely inside the configured dose window. It slides when that slice ages out or a dose-window write drops it. Changing the dose window does not clear the latch.

The latch is the busiest full slice since the previous consume. It does not fall when that slice leaves the dose window or the 400 s ring. Reading every byte of `0x44`..`0x47` or of `0x4C`..`0x4F` consumes it: the host receives the published value, then both latched registers reset unless a newer slice raised the latch after that snapshot was published. In that case the newer peak stays and is written into the bank immediately. The window pair is not cleared. Meshtastic’s `0x04`..`0x0B` read does not cover these bytes. A burst that stops at `0x3F` includes the span and does not consume the latch.

An accepted span write clears both peaks immediately. The same span written again is ignored. A short or out-of-range write is ignored. The next snapshots rebuild the window maximum from full slices of the new length.

## Inter-arrival histogram

The nine bins and the shortest gap count accepted gaps since `gm_counter_open_window()` at the start of this RUN, or since a histogram clear. They are not the rolling dose window at `0x16`. After that window fills, or after a host shortens it, the two numbers describe different spans. The first accepted edge has no gap, so the bin sum is one behind the RUN pulse count until a bin saturates or the histogram is cleared. Changing the window does not clear the bins. A holdoff write that is accepted, or a write of `01` at `0x38`, zeros the bins, the shortest gap, and the on-chip close log, and rebuilds the bin edges. Dose, CPM, and the count window stay. The next accepted edge starts a new gap series.

Holdoff `0` keeps the classic upper bounds `50, 100, 200, 500, 1000, 2000, 5000, 10000` µs. Holdoff `H` in `1..300` maps those bounds from `[0, 10000]` onto `[H, 10000]`:

```text
edge[i] = H + classic[i] * (10000 - H) / 10000
edge[7] = 10000
```

Integer multiply-add, rounded to nearest, then forced strictly increasing (`edge[0] > H`). Bin 0 is `[H, edge[0])`. Bin 8 is still `>= 10 ms`. The default 200 µs holdoff yields about `249, 298, 396, 690, 1180, 2160, 5100, 10000`. Labels are not on the bus; a host derives them from `0x20`. Each bin saturates at 65535 and sets bit 0 of `0x38`. That flag does not set dose `OVERFLOW`.

## Writes

A write is the pointer byte plus the value bytes. Short writes and out-of-range values are ignored. The previous setting stays.

| Pointer | Length | Value | Effect |
| --- | --- | --- | --- |
| `0x12` | 4 | non-zero Q16.16, big-endian | calibration. Zero is ignored |
| `0x1E` | 2 | seconds, 1..400, big-endian | static window |
| `0x20` | 2 | microseconds, 0..300, big-endian | holdoff. Also clears the histogram |
| `0x38` | 1 | `01` | clear the inter-arrival histogram |
| `0x3E` | 2 | seconds, 10..60, big-endian | peak span. Also clears both peaks |

The window, holdoff, and peak-span registers update when the write is accepted. Dose, CPM, count, and elapsed use the new window on the next one-second snapshot. Shrinking drops the oldest seconds. Lengthening keeps seconds still in the 400 s history and leaves `WINDOW_FULL` clear until the new length is filled. Holdoff applies to the next edge. Zero holdoff counts every falling edge. An accepted holdoff write clears the histogram immediately, same as `0x38`. A same-value holdoff write is ignored and does not clear. An accepted peak-span write clears both peaks immediately. A same-value span write is ignored and does not clear.

Accepted settings are stored in EEPROM with magic `0xC3`. Record version 2 includes the peak span. A valid version-1 record still loads window, holdoff, and calibration, and the span stays 10 s. A blank or bad record loads the defaults. `make flash` chip-erases EEPROM, so a freshly flashed board returns to 300 s, 200 µs, `604948`, and a 10 s peak span. The RadSens image uses the same EEPROM address with a different magic and does not read this record.

## Compatibility

| Image | Build | Address | Map |
| --- | --- | --- | --- |
| Native v2 | `make PROTOCOL=native` | `0x67` | this file |
| RadSens | `make PROTOCOL=radsens` | `0x66` | `i2c-register-map.md` |

One firmware image serves one map. The BME680 on this board stays at `0x76`. Address `0x67` does not collide with it. Long-interval cumulative counters are not part of protocol v2. The RadSens static window stays 300 s and its holdoff stays 200 µs. Its calibration is the sensitivity register at `0x12` on that map.

## Reads

ESP32-C3 serial bridge (`/mnt/Work/dev/PlatformIO/Projects/I2C_bridge`), 115200 baud:

```text
s
x 67 00 / 2
x 67 02 / 1
x 67 04 / 8
x 67 00 / 32
x 67 20 / 32
x 67 40 / 16
w 67 38 01
x 76 D0 / 1
```

That bridge accepts at most 32 bytes in one transfer. The 80-byte map is `x 67 00 / 32`, then `x 67 20 / 32`, then `x 67 40 / 16`. Each command writes the pointer first. The third span covers both latched peak registers and resets them. `0x39`..`0x3C` on this image is `00 02 01 00`.

A scan lists `0x67` and `0x76`. `x 67 00 / 2` returns `47 4E`. `x 67 02 / 1` returns `02`. `x 76 D0 / 1` returns `61`. Writing `00 09 3B 14` at `0x12` stores the default calibration. `w 67 38 01` clears the histogram. Writing `00 0A` at `0x3E` stores a 10 s peak span.
