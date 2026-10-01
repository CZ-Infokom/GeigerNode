# GeigerNode bring-up

Work in this order. Stop when this note says to stop. High voltage stays off. Fuses are read and saved, then left unchanged.

The firmware in this tree forces PB1 low and blinks PD7. Build it only after the fuse bytes are saved and `F_CPU` matches the clock those fuses select.

## Phase 0 — host toolchain

Recorded on this machine. Atmel-ICE was not connected for these checks.

| Tool | Result |
| --- | --- |
| `avr-gcc` | `/opt/avr8-gnu-toolchain-4.0.0/bin/avr-gcc` |
| version | `avr-gcc (AVR_8_bit_GNU_Toolchain_4.0.0_52) 15.1.0` |
| `avr-objcopy` | same prefix, `GNU objcopy (AVR_8_bit_GNU_Toolchain_4.0.0_52) 2.44` |
| `avr-size` | same prefix, `GNU size (AVR_8_bit_GNU_Toolchain_4.0.0_52) 2.44` |
| `-mmcu=atmega328pb` | accepted; defines `__AVR_ATmega328PB__` and `__AVR_DEVICE_NAME__ atmega328pb` |
| `avrdude` | `/usr/bin/avrdude`, version 8.1 |
| part id | `m328pb` |
| programmer id | `atmelice_isp` |

`avrdude -v` with no `-c` prints the version and then exits 1. That is this avrdude build, not a missing toolchain.

Repeat the host check from `firmware/`:

```sh
make toolchain-info
```

The `avr-gcc` 12.1.0 on `PATH` is a different install. Do not build with it.

Device signature, fuse bytes, and measured VCC are not recorded yet. Those are the board steps below, after Phase 1.

## Phase 1 — physical power check

Do this before connecting Atmel-ICE and before programming flash.

1. Visual inspection.
2. Check for solder bridges.
3. Check ATmega orientation.
4. Check regulator orientation.
5. Check Q1 / D1 orientation.
6. Check 3.3 V to GND resistance.
7. Check raw VCC to GND resistance.
8. Check ISP continuity (MOSI0, MISO0, SCK0, RESET, VCC sense, GND).
9. Power the board from a current-limited bench supply. Atmel-ICE does not power the target. External Grove VCC may be about 3.3 V or about 5 V; the onboard LDO makes the internal rail.
10. Verify approximately 3.3 V on the regulator output.
11. Verify ATmega VCC and AVCC.
12. Verify the BME680 supply.
13. Check that no component heats unexpectedly.
14. Check PB1 / HV_SW is not being driven.

Leave the high-voltage section unpowered. Do not install or energize it to test the MCU.

## Phase 0 — board reads

Connect Atmel-ICE ISP only after the Phase 1 checks pass. From `firmware/`:

```sh
make read-signature
```

The ATmega328PB signature is `0x1E 0x95 0x16`. If the bytes differ, stop. Do not flash.

```sh
make read-fuses
```

Save LFUSE, HFUSE, and EFUSE. This command only reads. Decode the saved LFUSE against the ATmega328PB datasheet before choosing a clock:

- CKSEL and SUT select the clock source and start-up time
- CKDIV8 programmed divides that clock by 8

A new ATmega328PB is often the internal 8 MHz RC oscillator with CKDIV8 programmed, so the CPU runs at 1 MHz. Treat that as a hint. The saved bytes are the source of truth. Do not change SPIEN, RSTDISBL, or DWEN. Do not write any fuse.

## Phase 2 — build and flash

After the signature matches and the fuse bytes are saved:

1. Choose `F_CPU` from the decoded clock. A firmware image built with `F_CPU=8000000UL` on a 1 MHz part stretches `_delay_ms(500)` by about 8x. The LED period is not proof of an 8 MHz clock.
2. Build and read the size:

```sh
make clean
make F_CPU=1000000UL
```

Use `8000000UL` only when the decoded fuses actually select 8 MHz with CKDIV8 unprogrammed.

3. Flash and verify. Both targets touch flash only:

```sh
make F_CPU=1000000UL flash
make F_CPU=1000000UL verify
```

Keep the same `F_CPU` you just built with. `make flash` rebuilds the hex if it is missing, and a bare `make flash` would use the Makefile default of 1 MHz.

4. Watch PD7. With a matching `F_CPU` the LED toggles every 500 ms.
5. Measure PB1. It stays low. The high-voltage converter stays inactive.
6. Supply current stays reasonable, and no part heats unexpectedly.

Phase 2 is passed only when all of the following are true:

- Atmel-ICE detects the target
- the signature is `0x1E 0x95 0x16`
- fuse bytes were read and the original values are saved
- the build has no warnings
- flash succeeds
- verify succeeds
- the LED blinks
- PB1 measures low
- the HV converter stays inactive
- supply current is reasonable
- no component heats unexpectedly

## Record

Write the results to `bringup/boards/board01.txt` (this directory). Create that file when the measurements exist. Include:

- board identifier or serial, if any
- measured VCC and the 3.3 V rail
- device signature
- LFUSE, HFUSE, EFUSE
- decoded clock and the `F_CPU` used to build
- avr-gcc version and avrdude version
- LED behavior
- PB1 level
- supply current, if measured

## Phase 4 — clock check

The approved fuses are already on this board: internal 8 MHz RC, CKDIV8 unprogrammed. Confirm that clock with a scope or logic analyzer on PD7. A visual blink is not the measurement.

`main` holds PB1 low and toggles PD7 from Timer2. Built with `F_CPU=8000000UL`, PD7 is a 500 Hz square wave (period 2 ms). The same image on a 1 MHz clock would be 62.5 Hz (period 16 ms).

```sh
make clean
make F_CPU=8000000UL flash
make F_CPU=8000000UL verify
```

Board 01 measured 500 Hz on PD7, and the Makefile default is now `8000000UL`.

## Phase 5A — LED

One peripheral at a time. This step is the LED only. PB1 stays low. Timer1, TWI, ADC, INT0, and PD3 stay unused.

Timer2 counts milliseconds. Main toggles PD7 every 500 ms, so the LED blinks visibly: 500 ms on, 500 ms off.

```sh
make clean
make flash
make verify
```

Board 01 passed this step: PD7 blinks 500 ms on and 500 ms off, and PB1 stays low.

## Phase 5B — GM pulse

PD2 / INT0 is an input with the internal pull-up enabled. A falling edge increments a counter. The interrupt does nothing else. Do not connect a Geiger tube for this test.

The LED keeps the 500 ms blink. Each new pulse also toggles PD7, so a burst of edges makes the LED flicker. A wire touched to ground will bounce, so one touch can count more than once.

PD2 is TQFP pin 32. With the board powered and the firmware running:

1. Leave the tube disconnected.
2. Briefly connect PD2 to GND.
3. Watch PD7 flicker, then return to the slow blink.
4. Confirm PB1 stays low.

```sh
make clean
make flash
make verify
```

Board 01 passed this step. Falling edges flicker the LED, then the slow blink returns. PB1 stays low. The tube stayed disconnected.

## Phase 5C — HV ADC, converter off

ADC0 uses the internal 1.1 V reference. The divider is 40 MOhm over 100 kOhm, so about 2 V at the HV node is only a few ADC counts. The converter stays off. Do not enable it for this test.

The LED no longer shows the 500 ms blink. Each sample starts with a longer flash, then one short flash per ADC count. Count the short flashes. A reading above about 10 V at the HV node leaves the LED on after the long flash and does not blink the count.

PC0 is TQFP pin 23. PB1 must stay low.

```sh
make clean
make flash
make verify
```

## Stop

I2C and HV generation stay out of scope:

- I2C
- HV PWM
- closed-loop HV regulation
