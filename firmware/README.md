# GeigerNode firmware

Bare-metal bring-up for the ATmega328PB on GeigerNode. Characterization is frozen: nominal target 380 V, pulse cap 100 µs, production abort around 400 V. This image is the SI-29BG RadSens slave. Every reset waits 5 s, regulates 380 V at 2400 Hz, counts every falling edge on PD2, and publishes ClimateGuard-compatible registers on TWI0 address `0x66`. The map is `i2c-register-map.md`. There is no arm word. Dose uses the accepted factory Cs-137 coefficient `S = 137.5 impulses/µR`. Dynamic radiation is the last 10 s. Static radiation is the last 60 s. During the count the LED is off and flashes on for each pulse. `make flash` programs flash only. `make read-gm-test` still reads EEPROM but this image does not write a GM record.

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

The Makefile default is `8000000UL`. PD7 on board 01 measured 500 Hz, which confirmed the 8 MHz clock. `main` rejects any other `F_CPU`.

Build output goes to `bin/`, which git ignores. `make` builds `bin/geigernode.hex` and prints `avr-size`. `make disasm` writes `bin/geigernode.lst`. `make clean` removes `bin/`.

## Program

The board is externally powered. Atmel-ICE does not power the target. Programmer settings are `-c atmelice_isp -P usb -p m328pb`.

```sh
make read-signature
make read-fuses
make flash
make verify
```

`make flash` and `make verify` program flash only. `make read-fuses` only reads LFUSE, HFUSE, and EFUSE. After `make flash` the programmer reset starts the 5 s delay, then high voltage. Stay clear of the HV section.

```sh
make fuses
```

That target writes only the approved bytes: LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`. It does not program flash. Do not change those three values without a new review.

Follow `bringup/README.md` before connecting the programmer or flashing.
