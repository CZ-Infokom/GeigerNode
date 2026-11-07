# GeigerNode firmware

Bare-metal bring-up for the ATmega328PB on GeigerNode. Characterization is frozen: nominal target 380 V, pulse cap 100 µs, production abort around 400 V. The I2C map is chosen at compile time: `make PROTOCOL=native` is protocol v2 at `0x67` (`geigernode-i2c-protocol.md`); `make PROTOCOL=radsens` is the ClimateGuard RadSens slave at `0x66` (`i2c-register-map.md`). Every reset waits 500 ms, regulates 380 V at 2400 Hz, and counts falling edges on PD2 except those inside 300 µs after an accepted edge. There is no arm word. Native dose is nSv/h from a Q16.16 nSv/h-per-CPM coefficient (SBM-20 datasheet midpoint 65 impulses/µR → 604948). RadSens dose uses the integer sensitivity register, default 65 impulses/µR. Static radiation defaults to the last 300 s. Native protocol v2 can set that window from 1 s to 400 s, the holdoff from 0 µs to 300 µs, and the calibration. RadSens keeps a fixed 300 s window and a fixed 300 µs holdoff. Native reads do not clear counters. A RadSens host can stop and start high voltage, change the address, and change the sensitivity. An operational HV fault retries four times, with the switch off for 30 s, then 2 min, then 10 min. An illegal pulse or rate, or a spent budget, stays off until a RadSens host writes `1` to `0x11` or the native image is reset. Hardware TWI0 is the slave. The bit-banged image is on `fallback/bitbang-i2c`. During the 500 ms wait the LED is on. During the count it flashes on for each pulse. `make PROTOCOL=native flash` or `make PROTOCOL=radsens flash` programs flash only and chip-erases EEPROM. `make read-gm-test` still reads EEPROM but this image does not write a GM record.

## Toolchain

Compiler tools come from `/opt/avr8-gnu-toolchain-4.0.0` when that prefix exists. This host falls back to `/opt/avr-gcc-15.2.0` (`avr-gcc` 15.2.0).

`avrdude` is the system binary (`/usr/bin/avrdude` 8.1). It is not shipped in the toolchain directory. This install needs `-C /etc/avrdude.conf`; the Makefile passes it. An `avr-gcc` 7.3.0 may exist on `PATH`; the Makefile calls the chosen prefix by absolute path.

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

`PROTOCOL` selects the I2C map. There is no default: pass `native` (`0x67`) or `radsens` (`0x66`). Each image writes `bin/<protocol>/`. `make math-test` runs the host golden vectors for CPM × 10 and nSv/h (no programmer, no HV, no `PROTOCOL`).

```sh
make PROTOCOL=native
make PROTOCOL=radsens
make math-test
```

Build output goes to `bin/`, which git ignores. `make PROTOCOL=native` builds `bin/native/geigernode.hex` and prints `avr-size`. `make PROTOCOL=native disasm` writes `bin/native/geigernode.lst`. `make clean` removes `bin/`.

## Program

The board is externally powered. The programmer does not power the target. Default programmer settings are Flipper AVR Flasher as STK500v1 on CDC `if02`: `-c stk500v1 -P /dev/serial/by-id/usb-Flipper_Devices_Inc.*-if02 -b 115200 -p m328pb`. Atmel-ICE is `-c atmelice_isp -P usb`.

```sh
make read-signature
make read-fuses
make PROTOCOL=native flash
make PROTOCOL=native verify
```

`make flash` and `make verify` program flash only. `make read-fuses` only reads LFUSE, HFUSE, and EFUSE. After `make flash` the programmer reset turns the LED on for 500 ms, then high voltage starts. Stay clear of the HV section. The chip erase wipes EEPROM. Native v1 does not load an I2C address from EEPROM. The RadSens image restores address `0x66` and sensitivity 65.

Standalone copies of the same flash images are `flipper/geigernode-native.hex` (8982 bytes) and `flipper/geigernode-radsens.hex` (9048 bytes). Copy one `.hex` file to the Flipper SD card and choose it in AVR Flasher's Flash AVR browser. Those files contain flash records only: no EEPROM, fuse, or lock data. Do not write fuses. A Flash AVR programming chip-erases EEPROM, then the reset starts 380 V. Details are in `flipper/README.md`.

```sh
make fuses
```

That target writes only the approved bytes: LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`. It does not program flash. Do not change those three values without a new review.

Follow `bringup/README.md` before connecting the programmer or flashing.
