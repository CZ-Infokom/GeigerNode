# GeigerNode firmware

Bare-metal bring-up for the ATmega328PB on GeigerNode. Characterization is frozen: nominal target 380 V, pulse cap 100 µs, production abort around 400 V. This image is the SI-29BG one-shot. It regulates 380 V at 2400 Hz, counts every falling edge on PD2 for 10 minutes, records each gap in one 1 µs histogram bin, and stores the first 16 gaps below 500 µs. It then turns HV off and writes one EEPROM record. Blanking is 0 µs. It does not reject pulses and it does not convert counts to dose. `make arm-gm-test` is the only arm. `make read-gm-test` only reads. Neither target writes fuses.

## Toolchain

Compiler tools come from `/opt/avr8-gnu-toolchain-4.0.0`:

- `avr-gcc` `AVR_8_bit_GNU_Toolchain_4.0.0_52` (GCC 15.1.0)
- `avr-objcopy` and `avr-size` from the same tree (binutils 2.44)

`avrdude` is the system binary (`/usr/bin/avrdude` 8.1). It is not shipped in that toolchain directory. An `avr-gcc` 12.1.0 may exist on `PATH`; the Makefile calls the 4.0.0 tools by absolute path.

```sh
make toolchain-info
```

## Build

`F_CPU` is the clock the compiler assumes for `_delay_ms()`. The fuse bytes set the real clock. Read and save the fuses, decode CKSEL and CKDIV8, then build with the matching value:

```sh
make F_CPU=1000000UL
make F_CPU=8000000UL
```

The Makefile default is `8000000UL`. PD7 on board 01 measured 500 Hz, which confirmed the 8 MHz clock. `main` rejects any other `F_CPU`. This image does not link TWI. The LED is off until the GM test is armed, blinks slowly for the 5 s pre-HV delay, blinks quickly while HV settles, stays on during the 10-minute count, and shows a grouped blink after a fault or abort. The bench steps are at the end of `bringup/README.md`.

Build output goes to `bin/`, which git ignores. `make` builds `bin/geigernode.hex` and prints `avr-size`. `make disasm` writes `bin/geigernode.lst`. `make clean` removes `bin/`.

## Program

The board is externally powered. Atmel-ICE does not power the target. Programmer settings are `-c atmelice_isp -P usb -p m328pb`.

```sh
make read-signature
make read-fuses
make flash
make verify
```

`make flash` and `make verify` program flash only. `make read-fuses` only reads LFUSE, HFUSE, and EFUSE.

```sh
make fuses
```

That target writes only the approved bytes: LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`. It does not program flash. Do not change those three values without a new review.

Follow `bringup/README.md` before connecting the programmer or flashing.
