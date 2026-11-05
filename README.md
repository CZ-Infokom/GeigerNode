# GeigerNode

Bare-metal firmware for the GeigerNode PCB: an ATmega328PB that runs an SBM-20 Geiger tube and answers as a ClimateGuard RadSens I2C slave at `0x66`.

This board generates about 400 V. Stay clear of the HV section when the LED is on after reset, and after `make flash`. The programmer reset holds the LED on for 500 ms, then 380 V starts.

## Current image

- Tube: SBM-20
- Sensitivity default: 65 impulses/µR (`0x41 0x00` at register `0x12`)
- Firmware byte `0x01`: `0x03`
- Operating point: 380 V at 2400 Hz, 100 µs pulse cap, production abort around 400 V
- Blanking: 0. Pulse counting does not scale by S.
- Host: Meshtastic / Flipper / any RadSens master

The JNA DR-M3 comparison has not been run. Do not replace 65 until that measurement is accepted.

## Repository layout

| Path | What it is |
| --- | --- |
| [`firmware/`](firmware/) | AVR sources, Makefile, and flash targets |
| [`firmware/i2c-register-map.md`](firmware/i2c-register-map.md) | RadSens register map |
| [`firmware/bringup/README.md`](firmware/bringup/README.md) | Bring-up log and host toolchain |
| [`GeigerNode Firmware Development.md`](GeigerNode%20Firmware%20Development.md) | Development plan and PROJECT STATUS |

## Build and flash

From `firmware/`:

```sh
make
make flash
make verify
```

The board is externally powered. Atmel-ICE does not power the target. `make flash` programs flash only and chip-erases EEPROM, which restores address `0x66` and sensitivity 65. Do not write fuses unless a human has approved the bytes. Details are in [`firmware/README.md`](firmware/README.md).

## Branches

| Branch | Role |
| --- | --- |
| `feature/*` | New work |
| `dev` | Integration |
| `master` | Tagged releases only |

Release path: `feature/*` → `dev` → `master` with a `vMAJOR.MINOR.PATCH` tag.

## Source

Private repository: [CZ-Infokom/GeigerNode](https://github.com/CZ-Infokom/GeigerNode)
