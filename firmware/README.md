# GeigerNode firmware

Bare-metal bring-up for the ATmega328PB on GeigerNode. This tree blinks the LED on PD7 and holds PB1 (HV_SW) low. It does not enable the high-voltage converter, and it cannot write fuses.

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

The Makefile default is `8000000UL`. PD7 on board 01 measured 500 Hz, which confirmed the 8 MHz clock. `main` rejects any other `F_CPU`. The LED blinks every 500 ms. TWI0 answers at address `0x66`: register `0x00` is `0x7D`, and registers `0x06` through `0x08` are `0x00 0x12 0x34`.

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
