# GeigerNode

Bare-metal firmware for the GeigerNode PCB: an ATmega328PB that runs an SBM-20 Geiger tube. The I2C map is chosen at compile time: `make PROTOCOL=native` is protocol v1 at `0x67`; `make PROTOCOL=radsens` is the ClimateGuard RadSens slave at `0x66`.

This board generates about 400 V. Stay clear of the HV section when the LED is on after reset, and after `make flash`. The programmer reset holds the LED on for 500 ms, then 380 V starts.

## Current image

- Tube: SBM-20
- Native protocol v2 at `0x67`: device ID `0x474E`, CPM × 10 and nSv/h. Default window 300 s, ceiling 400 s. Calibration Q16.16 `604948` (65 impulses/µR as nSv/h per CPM). Window, holdoff, and calibration are writable. Write `01` at `0x38` to clear the histogram; a holdoff write also clears it.
- RadSens image at `0x66`: firmware byte `0x01` is `0x04`, sensitivity default 65 impulses/µR, static window fixed at 300 s
- I2C: hardware TWI0. Bit-bang fallback is `fallback/bitbang-i2c`.
- Operating point: 380 V at 2400 Hz, 100 µs pulse cap, production abort around 400 V
- Holdoff: default 300 µs, native range 0..300 µs. Pulse counting does not scale by S.
- Host: Meshtastic reads native `0x04`..`0x0B`. RadSens masters need `PROTOCOL=radsens`.

The JNA DR-M3 comparison has not been run. Do not replace 65 until that measurement is accepted.

## Repository layout

| Path | What it is |
| --- | --- |
| [`firmware/`](firmware/) | AVR sources, Makefile, and flash targets |
| [`firmware/flipper/`](firmware/flipper/) | Flash-only Intel HEX images for the Flipper AVR Flasher |
| [`hardware/`](hardware/) | KiCad 10 rev A board. `hardware/production/` is the 2026-09-17 fab export |
| [`firmware/geigernode-i2c-protocol.md`](firmware/geigernode-i2c-protocol.md) | Native I2C protocol v1 |
| [`firmware/i2c-register-map.md`](firmware/i2c-register-map.md) | RadSens register map |
| [`firmware/bringup/README.md`](firmware/bringup/README.md) | Bring-up log and host toolchain |
| [`GeigerNode Firmware Development.md`](GeigerNode%20Firmware%20Development.md) | Development plan and PROJECT STATUS |

## Build and flash

From `firmware/`:

```sh
make PROTOCOL=native
make PROTOCOL=native flash
make PROTOCOL=native verify
```

The board is externally powered. Atmel-ICE does not power the target. `make flash` programs flash only and chip-erases EEPROM. Do not write fuses unless a human has approved the bytes. Details are in [`firmware/README.md`](firmware/README.md).

## Branches

| Branch | Role |
| --- | --- |
| `feature/*` | New work |
| `dev` | Integration |
| `master` | Tagged releases only |

Release path: `feature/*` → `dev` → `master` with a `vMAJOR.MINOR.PATCH` tag.

## Source

Private repository: [CZ-Infokom/GeigerNode](https://github.com/CZ-Infokom/GeigerNode)
