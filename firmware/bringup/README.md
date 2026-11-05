# GeigerNode bring-up

Current step: SBM-20 datasheet factor, flashed and verified 2026-11-05, 8902 bytes. Address `0x66`, device ID `0x7D`, firmware `0x03`, sensitivity `65` (`0x41 0x00`) unless EEPROM holds another pair. Static radiation is the last 500 s. Dynamic radiation is an adaptive window of 1..122 s. `0x09` clears on read. A host can write HV on/off, the LED enable, address `0x03`..`0x77`, and sensitivity `0`..`510`. Address and sensitivity are stored at EEPROM 960. `make flash` chip-erases that record and restores 65. Hardware TWI0 stays off and SCL is never driven. Dose uses the integer sensitivity register: µR/h = CPM * 60 / 65. The fitted tube is an SBM-20. Keep 380 V, 2400 Hz, the 100 µs cap, and zero blanking. Every reset holds the LED on for 500 ms, then high voltage. There is no arm word. Confirm the anode resistor is in series and is at least 1 MΩ before flashing; 5.1 MΩ is the datasheet recommendation. The JNA DR-M3 comparison has not been run. Stay clear of the HV section when flashing.

The sections below are the historical bring-up record. Fuses were written once and are not to be changed.

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

## SI-29BG 60 s pulse-path test

The tube is an SI-29BG. The SBM-20 is not on the board. This image proves the signal path and the regulator with the tube connected. It does not convert CPM to dose, and it does not reject closely spaced edges.

Operating point, fixed in the firmware:

| Limit | Value |
| --- | --- |
| Target | 380 V, 885 counts |
| Frequency | 2400 Hz |
| Pulse cap | 100 µs |
| Hard abort | 932 counts, about 400 V |
| Pre-HV delay | 5 s, HV off |
| Settle | within about ±5 V (12 counts) for 1 continuous second, or abort at 20 s |
| Count window | 600 s |
| EEPROM | one record at address 256, written only after PB1 is forced low |

`make flash` chip-erases EEPROM because EESAVE is unprogrammed. That erase removes the old frequency-series image. The series is already recorded above and on board 01. A blank GM record does not start HV. Do not run `make fuses`, `make arm-freq-series`, or `make read-freq-series` for this test.

Before applying power for the run:

1. Confirm the SI-29BG is mechanically secure.
2. Confirm tube polarity.
3. Confirm the HV ballast chain is in series with the tube.
4. Do not touch the tube or the HV section while it is energized.
5. Keep the DMM off the HV output. A meter there loads the converter and latches the regulator off.
6. If a scope is used, connect it only to PD2 / GM_PULSE and GND. Idle is high, near 3.3 V. An event is a low-going pulse. Do not probe the tube electrodes with an ordinary probe.
7. Current-limit the board supply.

From `firmware/`:

```sh
make
make flash
make verify
```

`make flash` programs flash only. HV stays off and the LED stays off, because the test is not armed.

When the bench is ready and everyone is clear of the HV section:

```sh
make arm-gm-test
```

That writes the arm word and checksum, or one new ARMED record if EEPROM has no valid GM record. It does not write fuses or flash. The programmer reset starts the run. Watch PD7:

| PD7 | Meaning |
| --- | --- |
| Off | Not armed. HV is off. |
| Slow blink, 500 ms | 5 s pre-HV delay. PB1 is low. |
| Fast blink, 100 ms | Regulator is starting or settling. |
| On, with brief dips | 10-minute measurement. A dip is one or more falling edges. The dip is generated in the main loop, not in the INT0 ISR. |
| On, steady | Window finished and HV is off. Read the record. |
| Four blinks and a pause | Fault or abort. HV is off. Read the record. |

The fast blink should give way to the measurement within a few seconds. The firmware allows 20 s to settle and then 10 minutes of counting. Wait until the LED has been steady, about 11 minutes after arming, or until the fault pattern is obvious. Do not reset the board and do not start an ISP read during the blinks. An ISP read resets the MCU. If the record is RUNNING, that reset forces PB1 low, stores ABORTED, and will not restart. Flash this image before arming. A version 1, 2, or 3 record does not start this firmware, and the previous image will not run a version 4 record.

Then:

```sh
make read-gm-test
```

That reads EEPROM only. It does not arm, does not start HV, and does not modify fuses. PB1 must be low afterward.

Interval bins are exclusive and timed at 1 µs. Each gap increments one bin: <50 µs, 50–100 µs, 100–200 µs, 200–500 µs, 500 µs–1 ms, 1–2 ms, 2–5 ms, 5–10 ms, and >=10 ms. A 0 µs gap is two edges in the same microsecond and lands in <50 µs. Those edges are counted. The bins saturate at 65535. Switching at 2400 Hz is 417 µs, so that spacing lands in 200–500 µs. A repeatable 100–300 µs cluster points at the tube or the pulse path. `GM_TEST_EXCESSIVE_FAST_PULSES` does not stop the window. It is stored when a finished window has at least 25 gaps under 1 ms, or when at least 30 pulses have more than half of their gaps under 2 ms. A few close edges stay `GM_TEST_OK` and are still visible in the bins.

A successful first run shows HV near 380 V, pulse width near the earlier 91 µs / 93 µs region, little or no time at 100 µs, some falling edges, no runaway, no HV fault, and PB1 low at the end. A background CPM below expectation is not a reason to change the setpoint.

If the measurement pulse count is zero, do not raise HV. Keep 380 V and check, in order:

1. Tube polarity.
2. Reservoir near 380 V from the EEPROM HV fields.
3. Tube-side HV through the ballast, with an HV-rated method, not an ordinary probe on the tube.
4. GM_PULSE / PD2 on the scope.
5. The Q2 detector path.
6. INT0 is falling-edge in this image.
7. To confirm the firmware itself counts, arm a run and briefly pull PD2 to ground during the measurement window. That falling edge must increase Measurement pulses. An edge during the 5 s delay increases Total GM pulses only. Do not reach into the HV section to do this.
8. Tube behavior only after the signal path is shown to count.

If the rate is unexpectedly high, do not treat it as radiation and do not add a software dead time. Look at the GM_PULSE waveform, at more than one falling edge per discharge, at HV-switching noise, at the sub-millisecond bins, and at grounding. The Q1 collector peak at this setpoint was about 427 V with a probe on the earlier unloaded run. Stop and remove power if that peak reaches about 450 V, if Q1, D1, or L1 heat is abnormal, or if the 3.3 V rail moves.

`make arm-gm-test` is the only way to run the test again. A completed or aborted record does not start HV on reset.

This image is the 1 µs inter-arrival run. It does not choose an SI-29BG sensitivity, convert CPM to dose, filter the rate, publish RadSens registers, or exercise I2C address 0x66.

## SI-29BG 60 s result

Passed on 2026-10-02. `make read-gm-test` reported GM_TEST_OK and EEPROM COMPLETE.

| Field | Value |
| --- | --- |
| Measurement pulses | 7 |
| CPM | 7 |
| Min / max CPS | 0 / 2 |
| Minimum interval | 0 ms, one pair, counted |
| Cumulative bins <1 / <2 / <5 / <10 ms | 1 / 1 / 1 / 1 |
| HV | 379 V, ADC 883–885 |
| Pulse width | 80 µs for the whole window |
| Time at 100 µs | 0 ms |

The unloaded 2400 Hz / 380 V hold had finished near 91 µs. This window held 80 µs and the ADC stayed on the 885-count target. The tube did not push the regulator toward the cap. The one same-tick pair is recorded and was not rejected. Do not add dead time for it, and do not raise the setpoint.

## SI-29BG 10-minute background run

Same setpoint and the same one-shot arm. The window is 600 s. CPM in the reader is `measurement pulses / 10`, shown to one decimal. Integer `measured_cpm` in EEPROM is that rate truncated. The raw pulse count is the value to keep. That run used the 1 ms tick, so a pair inside one tick is stored as 0 ms.

Flash, then arm only when the bench is clear:

```sh
make flash
make verify
make arm-gm-test
```

Leave the board alone until the LED has been steady. Then `make read-gm-test`. Do not read while the LED is blinking or while it is in the long on period.

## SI-29BG 10-minute result

Passed on 2026-10-02. `make read-gm-test` reported GM_TEST_OK and EEPROM COMPLETE.

| Field | Value |
| --- | --- |
| Measurement pulses | 112 |
| CPM | 11.2 |
| Min / max CPS | 0 / 6 |
| Minimum interval | 0 ms |
| Cumulative bins <1 / <2 / <5 / <10 ms | 6 / 8 / 8 / 8 |
| HV | 378–380 V, ADC 881–886, final count 885 |
| Pulse width | 80 µs for the whole window |
| Time at 100 µs | 0 ms |

The pulse width did not move, and it stayed under the cap. Six gaps were inside one millisecond tick and two more were 1 ms. The other 103 gaps were 10 ms or longer. Those eight close gaps were counted. They are not a reason to add dead time or to raise the setpoint. A one-second bucket of 6 counts is a cluster beside the 11.2 CPM average.

## SI-29BG 1 µs inter-arrival run

Same 380 V, 2400 Hz, and 100 µs cap. Pulse rejection stays off and dose conversion stays off. Timer0 stamps each falling edge at 1 µs. Timer1 remains the HV switcher. The histogram replaces the cumulative 1 ms bins. Version 3 of the EEPROM record is 72 bytes.

If a scope is available, clip it to GM_PULSE / PD2 and GND only, with persistence or single-shot trigger, during the measurement. The question is whether one tube event is one clean low-going pulse or sometimes two. Do not probe the tube.

Flash, then arm only when the bench is clear:

```sh
make flash
make verify
make arm-gm-test
```

Leave the board alone until the LED has been steady, about 11 minutes after arming. Then `make read-gm-test`. Do not read while the LED is blinking or while it is in the long on period.

## SI-29BG 1 µs result

Passed on 2026-10-02. `make read-gm-test` reported GM_TEST_OK and EEPROM COMPLETE.

| Field | Value |
| --- | --- |
| Measurement pulses | 85 |
| CPM | 8.5 |
| Min / max CPS | 0 / 2 |
| Minimum interval | 151 µs |
| <50 / 50–100 / 100–200 µs | 0 / 0 / 6 |
| 200–500 / 500 µs–1 ms | 0 / 0 |
| 1–2 / 2–5 / 5–10 / >=10 ms | 0 / 0 / 0 / 78 |
| HV | 377–381 V, ADC 879–888, final count 887 |
| Pulse width | 79–80 µs |
| Time at 100 µs | 0 ms |

The 84 intervals sum to one less than the 85 pulses. Every close gap is at least 151 µs and under 200 µs. Switching at 2400 Hz is 417 µs, and half of that is 208 µs. Both would land in 200–500 µs, which is empty. The repeatable 151–199 µs delay is the tube or the pulse path. Those six edges were counted. They are not a reason to add dead time or to raise the setpoint. 8.5 CPM against the earlier 11.2 CPM is the same background. The regulator took one step, from 80 µs to 79 µs, and never reached the cap.

## SI-29BG close-interval log

Same 380 V, 2400 Hz, and 100 µs cap. Every falling edge is still counted. Blanking stays 0 µs. Version 4 keeps the histogram and also stores the first 16 gaps below 500 µs, in arrival order. A published SI-29BG dead time is about 95 µs at 400 V, so a second edge near 151–200 µs can be a tube afterpulse. It can also be the Q2 detector crossing the logic threshold twice.

Clip the scope to GM_PULSE / PD2 and GND only before applying power at the bench. Trigger on the falling edge and capture about 500 µs to 1 ms after the event, with persistence or single-shot. Do not probe the tube.

An external reset leaves an ARMED record unstarted and holds PB1 low. The LED slow-blinks and high voltage stays off, including after `make arm-gm-test`. Remove board power, move the board, connect the scope, then apply power. That power-on is what starts the 5 s delay. Do not flash again after arming. A flash erases the arm. Do not remove power after the fast blink until the LED has been steady.

A normal event is one low pulse and then a clean high. A suspected pair is a second clean low pulse about 150–200 µs later. A ringing rising edge that crosses the threshold twice is the Q2 path.

Flash, then arm only when the bench is clear and the scope is on PD2:

```sh
make flash
make verify
make arm-gm-test
```

Leave the board alone until the LED has been steady. Then `make read-gm-test`. The reader lists each stored gap in microseconds.

## SI-29BG close-interval result

Passed on 2026-10-02. `make read-gm-test` reported GM_TEST_OK and EEPROM COMPLETE.

| Field | Value |
| --- | --- |
| Measurement pulses | 112 |
| CPM | 11.2 |
| Min / max CPS | 0 / 3 |
| Minimum interval | 151 µs |
| 100–200 µs / 200–500 µs | 10 / 0 |
| Gaps at or above 10 ms | 101 |
| Stored gaps, in order | 177, 151, 182, 160, 185, 161, 172, 183, 173, 164 µs |
| Sorted range | 151–185 µs |
| HV | 377–381 V, ADC 879–888, final count 886 |
| Pulse width | 79–80 µs |
| Time at 100 µs | 0 ms |

All ten short gaps were stored, and none of the other 101 gaps was under 10 ms. The same 151 µs floor appeared on the previous run. The values sit in a 34 µs band around about 171 µs. They do not fill the 100–200 µs bin, and the 417 µs switching bin is empty again. Blanking stayed 0. Removing those ten edges would turn 11.2 CPM into 10.2 CPM, so they stay in the raw count. The high-voltage regulator matches the previous window. This read does not include a PD2 waveform.

## SI-29BG source run

Passed on 2026-10-05 with a source nearby. `make read-gm-test` reported GM_TEST_EXCESSIVE_FAST_PULSES and EEPROM COMPLETE. The window finished. The flag is the background rule of 25 gaps under 1 ms. This run has 571 such gaps because the source produces the same companion edges seen at background.

| Field | Value |
| --- | --- |
| Measurement pulses | 7317, plus 1 before the window |
| CPM | 731.7 |
| Min / max CPS | 1 / 25 |
| Minimum interval | 145 µs |
| 100–200 µs / 200–500 µs | 510 / 27 |
| Stored gaps, first 16 | 154, 170, 381, 167, 157, 160, 153, 155, 164, 159, 160, 163, 154, 166, 234, 159 µs |
| HV | 376–383 V, ADC 878–893, final count 888 |
| Pulse width | 79–81 µs |
| Time at 100 µs | 0 ms |

Fourteen of the sixteen stored gaps are 153–170 µs. That is the same recovery delay as the background log. About 7% of the edges are that companion. The 27 gaps in 200–500 µs match ordinary spacing at 12 counts/s, so the 417 µs switching period is still absent as a peak. Blanking stayed 0. The reservoir moved one step wider than the background window and stayed off the 100 µs cap. This read does not include a PD2 waveform.

## SI-29BG scope-session window

Finished on 2026-10-05. This is the 10-minute record that was running during the PD2 persistence captures, with the source brought in and then held against the tube. `make read-gm-test` reported GM_TEST_EXCESSIVE_FAST_PULSES and EEPROM COMPLETE. The window finished.

| Field | Value |
| --- | --- |
| Measurement pulses | 6397 |
| CPM | 639.7 |
| Min / max CPS | 0 / 43 |
| Minimum interval | 146 µs |
| 100–200 µs / 200–500 µs | 296 / 50 |
| Stored gaps, first 16 | 155, 167, 162, 159, 168, 151, 157, 152, 147, 167, 146, 161, 164, 154, 151, 154 µs |
| HV | 374–383 V, ADC 873–894, final count 885 |
| Pulse width | 79–83 µs |
| Time at 100 µs | 0 ms |

All sixteen stored gaps are 146–168 µs. Nothing landed under 100 µs. A steady 10.7 counts/s would produce about 14 gaps under 200 µs across 6396 intervals; 296 were stored in that bin, the same recovery companion. Min CPS 0 is the portion with the source away, so this CPM is not a background number. HV minimum 374 V is ADC 873, the bottom of the ±5 V band, and the pulse stayed off the 100 µs cap. Blanking stayed 0.

## SI-29BG background after the scope session

Passed on 2026-10-05 with the source away. `make read-gm-test` reported GM_TEST_OK and EEPROM COMPLETE. The window finished. One edge arrived before the window opened.

| Field | Value |
| --- | --- |
| Measurement pulses | 75, plus 1 before the window |
| CPM | 7.5 |
| Min / max CPS | 0 / 2 |
| Minimum interval | 156 µs |
| 100–200 µs / 200–500 µs | 1 / 0 |
| Stored gaps | 156 µs |
| HV | 378–380 V, ADC 882–887, final count 885 |
| Pulse width | 80 µs |
| Time at 100 µs | 0 ms |

The only short gap is 156 µs, the same recovery delay as the earlier logs. A steady 7.5 CPM expects about 0.002 gaps under 200 µs, so this one edge is still the companion, and it is the only one in 75 pulses. The 417 µs switching bin is empty. Final ADC 885 is 380 V; the reader prints that count as 379 V. Blanking stayed 0. PD2 persistence on this run shows one long low pulse, a slow rise near 150–200 µs, and one narrow tick on that rise.

## Provisional tube sensitivity basepoints

These are engineering starting values, not traceable calibrations. Keep the raw
CPM available and keep the tube coefficient configurable.

For the previous SI-29BG, `S = 137.5 impulses/µR` was the provisional
Cs-137 datasheet basepoint. Published limits are 122.5–177.5 impulses/µR
(550 +160/-60 counts/s at 4 µR/s). The RadSens register is a whole number,
so that tube's register value is 138. It is not the live default. With the project conversion
`µR/h = CPM * 60 / S`, the nominal factors are:

- `µR/h = CPM * 0.43636`
- RadSens raw tenths of µR/h: `raw = CPM * 600 / 137.5`
- for orientation only, using 1 µR ≈ 0.00877 µSv for gamma in air:
  `µSv/h ≈ CPM * 0.003827`, or about 261 CPM per µSv/h

The published SI-29BG sensitivity is specified at 400 V. This board stays at
the frozen 380 V / 2400 Hz point. The published plateau slope is at most
0.125%/V, so the 20 V difference alone permits roughly 2.5% response change,
in addition to tube spread, source spectrum, geometry, and the board's
100–200 µs recovery companions. Do not change HV to match the datasheet.

The fitted tube is an SBM-20 (sometimes mistyped SBN20). The live default is
`S = 65 impulses/µR`, the Cs-137 midpoint. Published Cs-137 response is
240–280 counts/s at 4 µR/s, equivalent to 60–70 impulses/µR. The RadSens-module
Ra-226 default of 105 is not loaded. The nominal factors are:

- `µR/h = CPM * 0.92308`
- RadSens raw tenths of µR/h: `raw = CPM * 600 / 65`
- for orientation only: `µSv/h ≈ CPM * 0.008095`, or about 124 CPM per µSv/h

Published SBM-20 electrical values are 400 V recommended, 350–475 V operating
range, 190 µs dead time at 400 V, and 5.1 MΩ recommended anode resistance.
The board stays at 380 V and 2400 Hz, inside that operating range, with
blanking 0. The 400 V recommendation sits on the production abort and was
not applied. The 190 µs figure is not a software dead time. Confirm the
anode resistor before the first flash. The JNA DR-M3 comparison has not
been run. Do not replace 65 from one geometry. Record the source label
(nuclide, stated rate, distance), a background, and a source window long
enough to fill the 500 s static register. Keep raw counts from `0x09` as
well as `0x06`. `S_empirical = net_CPM * 60 / net_µR/h`.

Sources consulted 2026-10-05:

- [SI-29BG specification](https://www.etalonpribor.ru/catalog/rashodomeri/product/si-29bg_schetchik_geygera-myullera_/)
- [SI-29BG cross-check](https://gstube.com/data/2484/)
- [SBM-20 specification](https://gstube.com/data/2398)

## RadiaCode comparison checkpoint

A gas-lamp mantle can provide a repeatable check field, but it is not a
calibrated source. Do not burn, cut, crumble, or remove material from it. Keep
it sealed in its existing enclosure or a closed bag/container and minimize
handling. The RadiaCode comparison transfers that instrument's indicated
gamma dose rate under one spectrum and geometry; it does not create a
traceable calibration.

Use the existing 600 s GeigerNode window. Do not modify firmware, HV, blanking,
or dead time for this checkpoint.

1. Mark one position on the table. On 2026-10-05 the arrangement is flat:
   paper-wrapped mantle, 5 mm aluminum plate, and the detector 3 cm from the
   plate. RadiaCode reads about 200 nSv/h (0.20 µSv/h) there. Plate contact
   was about 0.8 µSv/h and is a different position. Mark the mantle, the
   plate, and the 3 cm detector point. The middle of the SI-29BG tube uses
   that same point.
2. Take the mantle to another room. Accumulate a RadiaCode spectrum at the
   marked spot for the same 600 s used by GeigerNode, and record its average
   µSv/h. Run one GeigerNode 600 s background window at that spot. Retain the
   raw pulse count.
3. Return the mantle and plate to their marks. Put the RadiaCode on the
   detector mark, start a fresh spectrum, and record the 600 s average µSv/h.
   Then put the SI-29BG tube center on that same mark and run one armed 600 s
   window. Do not ISP-read until the LED is steady.
4. Repeat that RadiaCode-then-GeigerNode pair once without moving the mantle
   or the plate. Record every spectrum, accumulation time, average µSv/h, and
   raw GeigerNode pulse count. Use the spectrum average, not the live display.
5. Subtract each instrument's own background before forming a ratio. Compare
   the two source pairs. A spread above 10% means the placement moved; do not
   replace the provisional coefficient from one pair alone.

Background for this checkpoint, recorded 2026-10-05 with the mantle in another
room: GeigerNode 91 measurement pulses, 9.1 CPM, GM_TEST_OK. RadiaCode
accumulated 10 nSv in 10 min, which is 0.060 µSv/h. Use these two values as
`background_CPM` and `background_RadiaCode_uSv_h`. The seven 154–170 µs gaps
stay in the raw count.

The 3 cm pair, 2026-10-05: 231 measurement pulses, 23.1 CPM, GM_TEST_OK, and
RadiaCode 20 nSv in 10 min, or 0.120 µSv/h. Both were at 3 cm. Net 14.0 CPM
and 0.060 µSv/h gives about 233 CPM per µSv/h and `S_empirical` about 123
impulses/µR. The live 200 nSv/h display was high. Keep 137.5. The direct position, 2026-10-05: RadiaCode 10-minute average 0.50 µSv/h, GeigerNode 650 measurement pulses, 65.0 CPM, GM_TEST_EXCESSIVE_FAST_PULSES. Net 55.9 CPM and 0.440 µSv/h gives about 127 CPM per µSv/h and `S_empirical` about 67 impulses/µR. Five gaps were under 50 µs, with 33 µs stored; 53 gaps were the usual 100–200 µs recovery. The results do not converge across geometry: 233 CPM per µSv/h at 3 cm versus 127 in contact. The 3 cm result is consistent with the published lower sensitivity limit, while contact is not a valid coefficient transfer because the radiation field varies sharply across the RadiaCode and the long SI-29BG sensitive volumes. The checkpoint is inconclusive, not evidence that the datasheet coefficient is wrong. Keep 137.5 impulses/µR. Do not add dead time or convert CPM to dose from this checkpoint.

For each paired run:

```text
delta_CPM = source_CPM - background_CPM
delta_uSv_h = source_RadiaCode_uSv_h - background_RadiaCode_uSv_h
GeigerNode_uSv_per_h_per_CPM = delta_uSv_h / delta_CPM
S_empirical_impulses_per_uR =
    0.5262 * delta_CPM / delta_uSv_h
```

The `0.5262` factor uses 1 µR ≈ 0.00877 µSv for gamma in air. Keep the direct
µSv/h-per-CPM ratio as the primary comparison result because RadiaCode reports
dose-equivalent rate rather than exposure. On 2026-10-05 the user accepted the
factory datasheet value `S = 137.5 impulses/µR` after the geometries failed to
converge.

## RadSens I2C

The live image is the SBM-20 RadSens slave in `i2c-register-map.md`. Default address `0x66`. Device ID `0x00` is `0x7D`. The firmware version at `0x01` is `0x03`. A master write of one byte sets the register pointer. Up to two data bytes after it are applied. The next read, after STOP or a repeated START, starts at that pointer. Bytes inside one read burst increment. Static radiation is `0x06..0x08`, 24-bit big-endian, unit 0.1 µR/h, last 500 s. Dynamic radiation at `0x03..0x05` is the adaptive window under 123 s. Conversion is `µR/h = CPM * 60 / S`, where `S` is the integer at `0x12`. The default is 65. Pulse count at `0x09..0x0A` is 16-bit big-endian and clears when either byte is read. `0x11` starts and stops high voltage. `0x10` and `0x12` are stored across reset until `make flash` erases EEPROM.

On 2026-10-05 a host read of `0x00` returned `0x00 0x00`, then `0x3B 0xAA` with the source present, and every other address returned those same two bytes. After the first pointer change every address returned `0x00`. That was the high byte of a leftover dose register. A read without a pointer byte now starts at `0x00`. On 2026-10-07 the Flipper bridge read `0x7D 0x01` from `0x00` and `0x8A 0x00` from `0x12` on the previous image. After the next flash:

```sh
i2cget -y <bus> 0x66 0x00
i2ctransfer -y <bus> w1@0x66 0x00 r2
i2ctransfer -y <bus> w1@0x66 0x03 r3
i2ctransfer -y <bus> w1@0x66 0x06 r3
i2ctransfer -y <bus> w1@0x66 0x12 r2
i2ctransfer -y <bus> w1@0x66 0x09 r2
```

Expect `0x7d`, then `0x7d 0x03`, then two 24-bit rates, then `0x41 0x00` unless a host has stored another sensitivity. The `0x09` read returns the pulses since the previous read and clears the counter. A second immediate read of `0x09` is zero if no new edge arrived.

`make flash` chip-erases EEPROM because EESAVE is unprogrammed. This image does not run the one-shot GM record. There is no arm word. Every reset, including the programmer reset after flash, holds the LED on for 500 ms and then starts high voltage. Address `0x66` answers during that wait. A stored address or sensitivity does not survive the chip erase.

From `firmware/`:

```sh
make
make flash
make verify
```

Stay clear of the HV section. The LED is on for 500 ms, off while 380 V settles, then off except for a flash on each count. A startup timeout, a settle timeout, or the 400 V abort turns the switch off and retries: four starts, with waits of 30 s, 2 min, and 10 min. During a wait the LED blinks once every 2 s. Four blinks and a pause means give-up; high voltage stays off until a write of `1` to `0x11`. An illegal pulse or rate gives up on the first failure. Do not raise the setpoint.

The 2026-10-05 pass saw address `0x66` and received values. Exact register bytes from that read were not recorded. The 2026-10-07 Flipper pass, on the previous image, returned `0x7D 0x01` and sensitivity `0x8A 0x00`. After the SBM-20 image is flashed, `0x12` is `0x41 0x00` unless EEPROM holds another pair. Background near 9 CPM is about 83 raw counts at S = 65 (8.3 µR/h). Do not treat a live reading as a calibration. The JNA DR-M3 comparison has not been run.

Keep the DMM off the HV output. Do not probe the tube. Do not change fuses. Do not add dead time. Do not raise 380 V or 2400 Hz.
