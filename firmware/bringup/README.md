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

Board 01 passed this step. The pattern was 1 long flash and 5 short flashes, raw 5, about 2.15 V at the HV node. The converter stayed off.

## Phase 5D — TWI slave

TWI0 is a slave at 7-bit address `0x66`. SDA is PC4 (TQFP pin 27) and SCL is PC5 (TQFP pin 28). The onboard 4.7 kOhm resistors pull those lines up to 3.3 V. The LED returns to a 500 ms blink so the firmware is visibly running.

Register `0x00` returns `0x7D`. Registers `0x06`..`0x08` return the fixed value `0x00 0x12 0x34`. A write sets the register pointer. A repeated START then read must use that pointer. Sequential reads advance it. The converter stays off.

```sh
make clean
make flash
make verify
```

From a 3.3 V I2C master on the internal bus, or from the host side through the BSS138 pair:

```sh
i2cdetect -y <bus>
i2cget -y <bus> 0x66 0x00
i2ctransfer -y <bus> w1@0x66 0x06 r3
```

Expect address `0x66` to answer, `0x00` to read `0x7d`, and the three-byte read to return `0x00 0x12 0x34`. PB1 stays low.

This bus read has not been done. TWI is untested.

## Phase 6 — coherent radiation registers

The same three bytes are published from one 24-bit value. A read latches all registers at the repeated START, so the three bytes cannot mix two updates. The bring-up value is still `0x001234`. The converter stays off.

```sh
make clean
make flash
make verify
```

The same `i2cget` and `i2ctransfer` checks apply. There is no new number to expect.

## Phase 7 — HV open-loop

The 500 Hz width sweep is finished. With the DMM connected, 100 µs produced 133.5 V DC. That number is a loaded reading. A meter near 10 MOhm on the HV output draws several times the current of the 40 MOhm / 100 kOhm divider, so 133.5 V is not the unloaded converter ceiling. Leave the DMM connected for the ADC correlation below. Do not disconnect it and run 100 µs.

The tube stays disconnected. Current-limit the board supply before any image that enables the pulse. TWI stays untested. The pulse-width cap stays 100 µs. The divider ratio stays 401:1.

The image on the chip is the one-shot frequency series. Results through 250 V at 500 Hz are already recorded. The hard abort is 815 counts, about 350 V. The pulse cap is 100 µs. The series never exceeds 1000 Hz. The temporary reference is 1095 mV.

Watch symbols, all global:

| Symbol | Meaning |
| --- | --- |
| `hv_adc_raw` | Last conversion in the 16-sample burst |
| `hv_adc_avg` | Integer mean of those 16 samples, raw counts |
| `hv_test_pulse_us` | 0 while the converter is held off. Otherwise the compiled width |
| `hv_test_frequency_hz` | 0 while the converter is held off. Otherwise the compiled rate |
| `hv_est_mv` | Nominal millivolts from `hv_adc_avg`, the nominal 1.1 V reference, and the nominal divider |

`hv_est_mv` is not a calibration. DWEN is unprogrammed, so debugWIRE is off. Do not change fuses to halt the core. ISP cannot read SRAM.

If debugger variables are unavailable, read PD7:

1. A 1.5 s dark gap starts the report.
2. One long pulse marks `hv_adc_raw`.
3. Four short-pulse groups encode its thousands, hundreds, tens, and ones digits.
4. A longer dark gap and two long pulses mark `hv_adc_avg`.
5. Four more short-pulse groups encode its four digits.
6. The LED then stays on. PB1 remains low.

Each decimal digit is encoded as digit plus one pulses: one pulse means 0,
two means 1, through ten pulses meaning 9. Leading zeroes are included.
For example, groups `1, 1, 1, 9` mean `0008`.

Nominal conversion, integer, divisor 1024:

```text
Vadc_uV = hv_adc_avg * 1100000 / 1024
HV_mV   = Vadc_uV * (40000000 + 100000) / 100000 / 1000
```

If the reference were exactly 1.100 V, which is not assumed, the loaded points already taken would sit near these counts:

| Loaded HV | Vadc | Raw count |
| --- | --- | --- |
| 43.7 V | 109 mV | 101 |
| 57.5 V | 143 mV | 133 |
| 105 V | 262 mV | 244 |
| 119.8 V | 299 mV | 278 |
| 133.5 V | 333 mV | 310 |

### ADC correlation, DMM still connected

Flash the default image first. PB1 stays low. After two seconds, count the
one-time LED report described above.

```sh
make clean
make flash
make verify
```

Record the OFF row while the LED is blinking. Then rebuild one point at a time. In `include/hv.h` set `HV_TEST_ENABLE` to 1 and `HV_PULSE_US` to the width for that row. Leave `HV_REP_HZ` at 500 and `HV_TEST_UNLOADED` at 0. Widths are 25, then 70, then 100. Do not use any other width.

While the LED is blinking, PB1 is pulsing and the 10 s window is open. Read
the DMM in that window. At the latch, PB1 is forced low before the one-time
LED ADC report starts. The LED stays on after the report. Reset restarts the
10 s window on an enabled image. An off image never starts it.

Worksheet. Leave the measured cells empty until the reading exists:

| Test | Pulse us | Hz | DMM HV | ADC raw | ADC avg | Estimated HV | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| OFF | 0 | — | | | | | DMM connected. Converter off |
| 25 us / 500 Hz | 25 | 500 | | | | | DMM connected |
| 70 us / 500 Hz | 70 | 500 | | | | | DMM connected |
| 100 us / 500 Hz | 100 | 500 | | | | | DMM connected. Hard width cap |

### Q1 collector, procedure only

Do this only after the four ADC rows above are filled. Use an HV-rated probe on the Q1 collector. Same limits: 500 Hz, 10 s latch, 100 µs hard cap, DMM still connected, unloaded abort still disabled.

1. 25 µs / 500 Hz.
2. If that waveform is safe, 70 µs / 500 Hz.
3. If that waveform is safe, 100 µs / 500 Hz.

At each point record:

- collector voltage while Q1 is on
- collector peak immediately after turn-off
- ringing peak
- ringing frequency
- approximate ring-down time
- 3.3 V rail during switching
- pulse width
- period

The HV output can be much lower than the collector ringing peak. Judge transistor stress from the collector waveform. If the collector peak approaches an unsafe margin for the transistor, stop. Do not add an RC or RCD snubber in this step. Do not continue to an unloaded run from this step.

## Frequency series

This 1000 Hz ladder already ran. The source tree no longer builds it. The image to flash next is the 1200 Hz series below.

Remove the DMM from the HV output before flashing. A meter on that node loads the converter. The tube stays disconnected. Current-limit the supply. Do not run 300 V at 500 Hz. The pulse cap stays 100 µs. The hard abort stays 815 counts, about 350 V. Frequency stays at or below 1000 Hz.

The upper-300 V ladder is three tests at 1000 Hz, each 10 s, with 10 s of HV off between them. A test that is not OK stops the remaining steps. 340 V is not a target, because the overshoot cut for that count is above the 815-count abort.

| Test | Target | Frequency | Target count |
| --- | --- | --- | --- |
| 0 | 310 V | 1000 Hz | 722 |
| 1 | 320 V | 1000 Hz | 746 |
| 2 | 330 V | 1000 Hz | 769 |

Timer1 stays in mode 14 with prescaler 1. `ICR1` is `F_CPU / frequency - 1`: 13332 at 600 Hz, 10665 at 750 Hz, and 7999 at 1000 Hz. Pulse width stays `OCR1A = microseconds * 8`, so 100 µs is 800 ticks at every frequency. At 1000 Hz that is 10% duty.

`make flash` chip-erases EEPROM because EESAVE is unprogrammed. Blank EEPROM arms this image once. The firmware writes `RUNNING` before the first pulse. A fast PD7 blink then lasts 5 s. Removing power during that blink aborts the series on the next boot. After the blink, test 0 starts by itself.

Do not run `make verify` after `make flash`. `make flash` already verifies the bytes it wrote. A later ISP reset while the series is `RUNNING` latches `ABORTED` and does not restart HV.

```sh
make clean
make flash
```

LED while the series is in progress:

| PD7 | Meaning |
| --- | --- |
| Fast blink | 5 s armed delay. HV is off. |
| On | A 10 s test is switching. |
| Slow blink | 10 s off interval. PB1 is low. |
| On, and it stays on | Series complete. HV is off. |
| Four blinks and a pause | Series aborted. HV is off. |

Wait until the LED has been on steadily well after the last slow-blink interval. The whole sequence is about 50 s plus the 5 s delay. Then, from `firmware/`:

```sh
make read-freq-series
```

That reads EEPROM and prints the three results. It does not arm the series and it does not write fuses. `COMPLETE` and `ABORTED` stay off across that reset.

`make arm-freq-series` is the explicit re-arm. It writes the arm word and checksum, or one new ARMED record if EEPROM has no valid series. It does not write flash or fuses. The programmer reset starts another 5 s delay. Do not use it unless another series was requested.

Saturation, timeout, and the 815-count abort each stop the rest of the ladder. An ADC average or raw sample at or above 815 counts latches the fault immediately.

## 1200 Hz series — already run

This series already ran and is recorded on board 01. The source tree no longer builds it. Do not flash from this section.

Q1 collector at about 330 V HV, 2026-10-01: peak about 360 V. The device is an STR1550, VCEO 500 V. That point has about 140 V of margin. Turn-off overshoot was about 30 V, with damped ringing. Do not treat +30 V as constant at a higher output. If the collector peak reaches about 450 V, remove power and do not continue.

Remove the DMM from the HV output before flashing. The tube stays disconnected. Current-limit the supply. Use an HV-rated probe on the Q1 collector for the whole series.

| Test | Target | Frequency | Target count |
| --- | --- | --- | --- |
| 0 | 340 V | 1200 Hz | 792 |
| 1 | 350 V | 1200 Hz | 815 |
| 2 | 360 V | 1200 Hz | 839 |
| 3 | 380 V | 1200 Hz | 885 |

Timer1 stays in mode 14 with prescaler 1. `ICR1` is `F_CPU / 1200 - 1` = 6665. The period is 6666 counts, so the switching rate is 8000000/6666 Hz, about 1200.12 Hz. Pulse width stays `OCR1A = microseconds * 8`. 100 µs is 800 ticks, about 12.0% duty at this rate. The pulse cap stays 100 µs. The hard abort is 995 counts, about 427 V, and it is not a target. 400 V is not in this series.

Each test runs 10 s. HV is off for 10 s between tests. Saturation is recorded and the series continues. An ADC average or raw sample at or above 995 counts stops the rest of the series.

The results are on board 01. Do not flash this series again.

| PD7 | Meaning |
| --- | --- |
| Fast blink | 5 s armed delay. HV is off. |
| On | A 10 s test is switching. |
| Slow blink | 10 s off interval. PB1 is low. |
| On, and it stays on | Series complete. HV is off. |
| Four blinks and a pause | Series aborted. HV is off. |

The sequence is about 70 s plus the 5 s delay. Then, from `firmware/`:

```sh
make read-freq-series
```

That reads EEPROM and does not arm another run. `make arm-freq-series` is the only re-arm. It does not write flash or fuses.

Stop and remove power if the Q1 collector peak reaches about 450 V, if ringing grows much larger than at 330 V, if the waveform becomes irregular, if Q1, D1, or L1 heat is abnormal, or if the 3.3 V rail becomes unstable. Do not continue to 400 V.

Worksheet. Leave the measurement cells empty until the reading exists:

| Target | Hz | Final HV | Max HV | Final pulse | Max pulse | ms at 100 us | Q1 peak | Ringing notes | 3.3 V notes | Thermal notes | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 340 V | 1200 | | | | | | | | | | |
| 350 V | 1200 | | | | | | | | | | |
| 360 V | 1200 | | | | | | | | | | |
| 380 V | 1200 | | | | | | | | | | |

## 2000 Hz ladder — already run

Q1 collector at the 380 V / 1500 Hz point measured 411 V. The device is an STR1550, VCEO 500 V. That reading is under the 450 V stop. This ladder already ran and is recorded on board 01. Do not flash it again.

Remove the DMM from the HV output. The tube stays disconnected. Current-limit the supply.

| Test | Target | Frequency | Target count |
| --- | --- | --- | --- |
| 0 | 350 V | 2000 Hz | 815 |
| 1 | 360 V | 2000 Hz | 839 |
| 2 | 370 V | 2000 Hz | 862 |
| 3 | 380 V | 2000 Hz | 885 |
| 4 | 390 V | 2000 Hz | 909 |
| 5 | 400 V | 2000 Hz | 932 |

Timer1 stays in mode 14 with prescaler 1. `ICR1` is `F_CPU / 2000 - 1` = 3999. The period is 4000 counts, so the switching rate is exactly 2000 Hz. Pulse width stays `OCR1A = microseconds * 8`. 100 µs is 800 ticks, 20% duty at this rate. The pulse cap stays 100 µs. The hard abort is 995 counts, about 427 V, and it is not a target. The controller is unchanged. Frequency stays 2000 Hz inside each test.

Each test runs 10 s. HV is off for 10 s between tests. Saturation is recorded and the ladder continues. An ADC average or raw sample at or above 995 counts stops the rest of the series. There is no automatic restart.

`make flash` chip-erases EEPROM because EESAVE is unprogrammed. Blank EEPROM arms this image once. The firmware writes `RUNNING` before the first pulse. A fast PD7 blink then lasts 5 s. Removing power during that blink aborts the series on the next boot.

The results are on board 01. Do not flash this ladder again.

| PD7 | Meaning |
| --- | --- |
| Fast blink | 5 s armed delay. HV is off. |
| First solid interval | 350 V |
| Slow blink | 10 s off. PB1 is low. |
| Next solid intervals | 360 V, 370 V, 380 V, 390 V, then 400 V |
| On, and it stays on | Series complete. HV is off. |
| Four blinks and a pause | Series aborted. HV is off. |

The sequence is about 110 s plus the 5 s delay. Wait until the LED has been on steadily after the last slow blink, about 140 s after flash. Then, from `firmware/`:

```sh
make read-freq-series
```

That reads EEPROM and does not arm another run. `make arm-freq-series` is the only re-arm. It does not write flash or fuses.

Stop and remove power if the Q1 collector peak reaches about 450 V, if ringing grows much larger than at 380 V, if the waveform becomes irregular, if Q1, D1, or L1 heat is abnormal, or if the 3.3 V rail becomes unstable. Removing power aborts the remaining steps on the next boot.

## Stop

The 1400/1500 Hz series is COMPLETE. 360 V at 1400 Hz held 93 µs. 380 V at 1500 Hz held 99 µs. Q1 collector at that 380 V point measured 411 V. The pulse cap stays 100 µs. The 2000 Hz ladder is COMPLETE. 380 V held 96 µs. 390 V and 400 V both sit at about 389 V and 100 µs. Scope peaks from GND to the Q1-diode net were 395 V, 407 V, 415 V, 427 V, 439 V, and 439 V. The 3.3 V rail stayed stable. Ringing was present, well behaved, and fell off fast. Temperature was OK. The 380 V sweep from 2000 Hz to 2500 Hz is COMPLETE. Holds were 96, 95, 94, 92, 91, and 90 µs. Q1-diode peaks were 423 V, then 427 V. The 2500 Hz ladder from 350 V to 400 V is COMPLETE. 400 V held 97 µs. Q1-diode peaks were 391 V, 403 V, 415 V, 427 V, 435 V, and 447 V. The 3.3 V rail, ringing, and temperature were OK. The 400 V sweep from 2500 Hz to 3000 Hz is COMPLETE. Holds were 85, 84, 95, 94, 93, and 92 µs. All six are HV_TEST_OK. The Q1-diode peak was 447 V on every step. Ringing subsided well in time. The 3.3 V rail stayed stable. Temperature was OK. Characterization is frozen: nominal target 380 V, nominal frequency about 2400–2500 Hz, pulse cap 100 µs, production abort around 400 V. The Q1-diode peak at 380 V was about 427 V with the probe attached. The 995-count abort was characterization only. Do not change fuses.
