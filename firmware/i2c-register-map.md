# GeigerNode I2C register map (RadSens)

This is the ClimateGuard RadSens map at `0x66`, built with `make PROTOCOL=radsens`. Protocol v2 at `0x67` is `make PROTOCOL=native`; see `geigernode-i2c-protocol.md`. The static window on this map stays 300 s. Holdoff stays 300 µs. Neither is a register here.

Slave address `0x66` until a host changes it. Hardware TWI0 is the slave on PC4 (SDA) and PC5 (SCL). The bit-banged image remains on `fallback/bitbang-i2c`.

A master write's first data byte is the register pointer. Up to two data bytes after that are applied. Further bytes in that write are acknowledged and ignored. A pointer of `0x15` or higher is stored as `0x00`. A read with no pointer byte starts at `0x00`. Bytes inside one read increment. After `0x14`, further bytes in that burst are `0xFF`.

`0x11` reads `1` from reset, including the 500 ms wait, while 380 V is settling, and during an HV cooldown. It reads `0` after give-up. Radiation registers stay zero until the tube is in run, then update once per second. A new run clears the windows. `0x09` counts only while the tube is in run. `0x0B` is the HV status.

## Map

| Address | Name | Access | Value |
| --- | --- | --- | --- |
| `0x00` | Device ID | read | `0x7D` |
| `0x01` | Firmware version | read | `0x04` |
| `0x02` | Reserved | read | `0x00` |
| `0x03`..`0x05` | Dynamic radiation | read | 24-bit big-endian, 0.1 µR/h, adaptive window under 123 s |
| `0x06`..`0x08` | Static radiation | read | 24-bit big-endian, 0.1 µR/h, last 300 s |
| `0x09`..`0x0A` | Pulse count | read | 16-bit big-endian. Cleared by a read of either byte |
| `0x0B` | HV status | read | Low nibble `0` following the command, `1` cooldown, `2` given up. High nibble is attempts already used |
| `0x0C` | Low-power control | read/write | `0` or `1`. Default `0`. The bit is stored and does not change voltage, frequency, or counting |
| `0x0D`..`0x0F` | Reserved | read | `0x00` |
| `0x10` | I2C address | read/write | Default `0x66`. A write of `0x03`..`0x77` is stored and used on the next transaction. `0x76` collides with the BME680 on this board |
| `0x11` | HV generator | read/write | `1` on, `0` off. Default `1`. Other values are ignored. Give-up forces `0`. A later write of `1` starts a new budget |
| `0x12`..`0x13` | Sensitivity | read/write | little-endian impulses/µR, range `0`..`510`. Default `65` (`0x41 0x00`) |
| `0x14` | LED enable | read/write | `1` flashes on each pulse, `0` stays off. Default `1`. Other values are ignored |

## Radiation

Both radiation values are dose rate. Each second the firmware adds that second's counts. `0x06` keeps the last 300 s. Until 300 s exist, the rate uses only the seconds collected so far.

`0x03` keeps a window of 1..122 s. It grows by one second while it is shorter than 122 s. It shrinks to the last 10 s when that slice and the older part differ by more than a factor of two and the busier side has at least 4 counts. ClimateGuard did not publish the thresholds; this is the local approximation of the dynamic window.

The unit is 0.1 µR/h. Divide the integer by 10 for µR/h, or by 1000 for µSv/h. `00 00 3C` is 60, which is 6.0 µR/h, or 0.060 µSv/h. The register saturates at 1,440,000 (144,000.0 µR/h).

The conversion uses the integer in `0x12`:

```text
µR/h = CPM * 60 / S
register = µR/h * 10
```

`S = 0` yields 0. The fitted tube is an SBM-20. Reset publishes 65 impulses/µR, the Cs-137 datasheet midpoint. The SI-29BG factory value remains 137.5, and its register value is 138; that is not the live default. A host write replaces the live value. One data byte updates that byte. Two data bytes at `0x12` are little-endian. A result above 510 is ignored. Address and sensitivity are saved at EEPROM address 960. `make flash` chip-erases EEPROM and restores `0x66` and 65. HV, the LED enable, and `0x0C` return to their defaults on reset.

`0x09` is the number of pulses since the previous read of `0x09` or `0x0A`, saturated at 65535. The read clears it. A `0x0A` that follows `0x09` in the same burst is the low byte of that same snapshot. It is not the count inside the radiation windows.

## Controls

Write `0` to `0x11` to stop the converter. Write `1` to start it. The 500 ms wait after reset still finishes before the first start. A later write of `1` starts immediately and still waits until 380 V has held the settle band before counts begin.

A startup timeout, a settle timeout, or the 400 V abort drops the switch and retries. There are four starts. Between them the switch stays off for 30 s, then 2 min, then 10 min. `0x11` stays `1` during those waits, and `0x0B` reads `1` in the low nibble. The fourth failure gives up: `0x11` is `0`, `0x0B` low nibble is `2`, and the LED blinks four times and pauses. An illegal pulse width or switching rate gives up on the first failure. Ten continuous minutes in run clears the attempt count. A write of `0` during a cooldown cancels the pending retry. A write of `1` after give-up starts a new budget of four. The high nibble of `0x0B` is the attempts already used.

`0x14` does not follow the LED pin. During the 500 ms wait the LED is on while this register is `1`, so the reset is visible. It is off while 380 V settles and while the generator is off. In run it flashes on for each pulse when the register is `1`. A cooldown blinks once every 2 s. A give-up blinks the fault pattern either way.

## Reads

ESP32-C3 serial bridge (`/mnt/Work/dev/PlatformIO/Projects/I2C_bridge`), 115200 baud:

```text
s
r 66 00 2
r 66 06 3
r 66 09 2
r 66 00 21
r 76 D0 1
```

Flipper I2C bridge, with the app running:

```text
i2c_read 0x66 0x03 3
i2c_read 0x66 0x06 3
i2c_read 0x66 0x09 2
i2c_read 0x66 0x00 21
```

The `0x09` read and the 21-byte read both clear the pulse counter.

Linux i2c-tools:

```sh
i2ctransfer -y <bus> w1@0x66 0x03 r3
i2ctransfer -y <bus> w1@0x66 0x06 r3
i2ctransfer -y <bus> w1@0x66 0x09 r2
i2cget -y <bus> 0x66 0x00
```

`i2cget` of `0x00` returns `0x7d`. A 21-byte read from `0x00` returns device ID `0x7D`, firmware `0x04`, address `0x66` at `0x10`, and sensitivity `0x41 0x00`, unless a host has stored another address or sensitivity. `0x0B` is `0x00` until the first HV start.
