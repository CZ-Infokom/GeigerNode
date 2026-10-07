# GeigerNode firmware

Bare-metal bring-up for the ATmega328PB on GeigerNode. Characterization is frozen: nominal target 380 V, pulse cap 100 µs, production abort around 400 V. This image is the SI-29BG RadSens slave. Every reset waits 500 ms, regulates 380 V at 2400 Hz, counts every falling edge on PD2, and publishes ClimateGuard RadSens registers. The default address is `0x66`. The map is `i2c-register-map.md`. There is no arm word. Dose uses the integer sensitivity register, default 138 impulses/µR. Dynamic radiation is an adaptive window under 123 s. Static radiation is the last 500 s. A host can stop and start high voltage, change the address, and change the sensitivity. An operational HV fault retries four times, with the switch off for 30 s, then 2 min, then 10 min. An illegal pulse or rate, or a spent budget, stays off until the host writes `1` to `0x11`. Firmware version is `0x02`. During the 500 ms wait the LED is on. During the count it flashes on for each pulse while the LED register is enabled. `make flash` programs flash only and chip-erases EEPROM, which restores address `0x66` and sensitivity 138. `make read-gm-test` still reads EEPROM but this image does not write a GM record.

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

`make flash` and `make verify` program flash only. `make read-fuses` only reads LFUSE, HFUSE, and EFUSE. After `make flash` the programmer reset turns the LED on for 500 ms, then high voltage starts. Stay clear of the HV section. The chip erase restores I2C address `0x66` and sensitivity 138.

```sh
make fuses
```

That target writes only the approved bytes: LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`. It does not program flash. Do not change those three values without a new review.

Follow `bringup/README.md` before connecting the programmer or flashing.
