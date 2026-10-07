# GeigerNode I2C register map

Slave address `0x66`. This is the ClimateGuard RadSens subset the current firmware publishes. Hardware TWI0 is off. The slave is bit-banged on PC4 (SDA) and PC5 (SCL) and never drives SCL.

A master write's first data byte is the register pointer. Every data byte after that is acknowledged and ignored, so an I2C write cannot start or stop high voltage, change the address, or change the sensitivity. A pointer of `0x15` or higher is stored as `0x00`. A read with no pointer byte starts at `0x00`. Bytes inside one read increment. After `0x14`, further bytes in that burst are `0xFF`.

Radiation registers stay zero through the 5 s delay and while 380 V is settling. They update once per second after the tube is in run. A reset clears the windows.

## Map

| Address | Name | Access | Value |
| --- | --- | --- | --- |
| `0x00` | Device ID | read | `0x7D` |
| `0x01` | Firmware version | read | `0x01` |
| `0x02` | Reserved | read | `0x00` |
| `0x03`..`0x05` | Dynamic radiation | read | 24-bit big-endian, 0.1 µR/h, last 10 s |
| `0x06`..`0x08` | Static radiation | read | 24-bit big-endian, 0.1 µR/h, last 60 s |
| `0x09`..`0x0A` | Pulse count | read | 16-bit big-endian, counts since run started |
| `0x0B` | Reserved | read | `0x00` |
| `0x0C` | Low-power control | read | `0x00`. Writes are ignored |
| `0x0D`..`0x0F` | Reserved | read | `0x00` |
| `0x10` | I2C address | read | `0x66` |
| `0x11` | HV state | read | `0` before run, `1` while the tube is in run. Writes are ignored |
| `0x12`..`0x13` | Sensitivity | read | little-endian `138` (`0x8A 0x00`), impulses/µR |
| `0x14` | LED state | read | `0` before run, `1` while the tube is in run. This byte does not follow the per-pulse flash |

## Radiation

Both radiation values are dose rate, not an average of earlier readings. Each second the firmware adds that second's counts and drops the oldest second once the window is full.

`0x03` uses the last 10 one-second buckets. `0x06` uses the last 60. Until that many seconds exist, the rate uses only the seconds collected so far. The first minute of `0x06` is a growing window, not a 60 s window padded with zeros.

The unit is 0.1 µR/h. Divide the integer by 10 for µR/h, or by 1000 for µSv/h. `00 00 3C` is 60, which is 6.0 µR/h, or 0.060 µSv/h.

The conversion uses `S = 137.5` impulses/µR:

```text
µR/h = CPM * 60 / 137.5
register = µR/h * 10
```

`0x12` stores the nearest whole number, 138. Dose math uses 137.5. The register cannot be written.

`0x09` is the number of pulses since run started, saturated at 65535. A read does not clear it. It is not the count inside the 10 s or 60 s window.

## Reads

Flipper I2C bridge, with the app running:

```text
i2c_read 0x66 0x03 3
i2c_read 0x66 0x06 3
i2c_read 0x66 0x09 2
i2c_read 0x66 0x00 21
```

Linux i2c-tools:

```sh
i2ctransfer -y <bus> w1@0x66 0x03 r3
i2ctransfer -y <bus> w1@0x66 0x06 r3
i2ctransfer -y <bus> w1@0x66 0x09 r2
i2cget -y <bus> 0x66 0x00
```

`i2cget` of `0x00` returns `0x7d`. A 21-byte read from `0x00` returns device ID `0x7D`, firmware `0x01`, address `0x66` at `0x10`, and sensitivity `0x8A 0x00`.
