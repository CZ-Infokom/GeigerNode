# GeigerNode firmware

Bare-metal bring-up for the ATmega328PB on GeigerNode. Characterization is frozen: nominal target 380 V, nominal frequency about 2400–2500 Hz, pulse cap 100 µs, production abort around 400 V. This source holds 380 V at 2500 Hz after a 5 s delay. That hold has been run: the Q1-diode peak was 427.4 V and thermals were OK. A meter on the HV output loads the converter and latches the hold off. `make read-freq-series` prints the record. `make arm-freq-series` is the only re-arm. It cannot write fuses.

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

The Makefile default is `8000000UL`. PD7 on board 01 measured 500 Hz, which confirmed the 8 MHz clock. `main` rejects any other `F_CPU`. A fast PD7 blink is the 5 s armed delay. The LED stays on while a test is switching, blinks slowly during each 10 s off interval, stays on when the series is complete, and shows a grouped blink after an abort. TWI0 is in the image at address `0x66`, and that bus is untested.

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
