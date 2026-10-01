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

The Makefile default is `1000000UL`, for a part whose 8 MHz RC oscillator is still divided by 8. Change it when the saved fuses say otherwise. A default of 8 MHz is not used until that clock is actually programmed and approved.

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

That target always fails. Fuse programming stays a separate step after the saved bytes are decoded and a person approves the new values.

Follow `bringup/README.md` before connecting the programmer or flashing.
