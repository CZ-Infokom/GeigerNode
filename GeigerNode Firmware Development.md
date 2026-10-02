# GeigerNode Firmware — Master Development Prompt

You are developing firmware and performing hardware bring-up for a custom PCB called GeigerNode.

The firmware must be written for bare AVR silicon using the GNU AVR toolchain. Do not introduce Arduino, PlatformIO, vendor HALs, bootloaders, or unnecessary abstraction layers.

The immediate goal is SAFE HARDWARE BRING-UP of newly manufactured PCBs using an Atmel-ICE programmer.

Do not jump ahead to the complete firmware. Work through the bring-up phases in order and stop at explicit checkpoints.

Agent directives for this repository live in `.cursor/rules/`. This file remains the plan. The PROJECT STATUS section below is the persistence layer: update it when the phase, a checkpoint, a decision, or the stopping point changes. Numbered hardware, safety, and fuse sections stay as written unless the user changes those requirements.

======================================================================
PROJECT STATUS
======================================================================

Last updated: 2026-10-02

Where we are:
- HV characterization is closed on 2026-10-02. The frozen limits are the nominal tube target of 380 V, a nominal frequency of about 2400–2500 Hz, an absolute pulse cap of 100 µs, and a production HV abort around 400 V (932 counts). The 380 V / 2500 Hz production hold passed on 2026-10-02. The Q1-diode peak measured 427.4 V with the probe attached. Thermals were OK. A meter on the HV output loads the converter and latches the hold off. The GM tube stays disconnected.
- Work is on local `feature/bringup`, branched from local `dev`. The characterization through the frozen 380 V limits is committed on this branch as `70c472d`. TWI is untested. `dev` and `main` do not have that commit yet.
- Phase 0 host toolchain is recorded in `firmware/bringup/README.md`. Compiler is `/opt/avr8-gnu-toolchain-4.0.0` (`avr-gcc` `AVR_8_bit_GNU_Toolchain_4.0.0_52`, GCC 15.1.0). `avrdude` is system 8.1 at `/usr/bin/avrdude`. `-mmcu=atmega328pb` works.
- Section 33 skeleton is in the tree: `firmware/Makefile`, `firmware/README.md`, `firmware/bringup/README.md`, `firmware/include/board.h`, `firmware/src/main.c`. PB1 is forced low and PD7 blinks. `make flash` programs flash only.
- Host build succeeds with no warnings (`F_CPU=1000000UL`, 240 bytes text). That image was flashed and verified on 2026-10-01.
- Phase 2 has passed. Signature, fuses, flash, verify, PD7 blink, PB1 low, 3.3 V rail, supply current, no heating, and HV converter inactive are recorded in `firmware/bringup/boards/board01.txt`.
- Phase 3 fuses were written on 2026-10-01 with `make fuses` and read back as LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`. Signature remained `0x1E9516`. `make flash` still programs flash only.
- Phase 4 passed. Before the timer image, the 1 MHz blink firmware flashed at 8 Hz. After `make F_CPU=8000000UL flash` (260 bytes, verified), PD7 measured 500 Hz. The Makefile default is now `8000000UL`.
- Phase 5A passed on 2026-10-01. PD7 blinks with 500 ms on and 500 ms off, and PB1 remains low.
- Phase 5B passed on 2026-10-01. PD2 falling edges increment the counter, and the LED flickers when they arrive. The tube stayed disconnected.
- Phase 5C passed on 2026-10-01. ADC0 read raw 5 with the converter off (1 long flash, 5 short flashes). Nominal conversion is 2.15 V at the HV node; the meter earlier read 2.1 V.
- Phase 5D TWI is untested. No master was connected. Do not treat address `0x66` as verified.
- Phase 6 publishes static radiation from one 24-bit snapshot. A TWI read latches the register bank so `0x06`..`0x08` cannot tear. The value is still `0x001234`. That image has not been flashed.
- Phase 7 width sweep at 500 Hz is finished at the 100 µs cap: 133.5 V DC with the DMM connected. Earlier points: 43.7 V at 25 µs, 57.5 V at 40 µs, 105 V at 70 µs, 100.5 V at 70 µs / 800 Hz, and 119.8 V at 85 µs. Those results are not committed. 133.5 V is a loaded reading, not the unloaded ceiling. Do not disconnect the DMM and run 100 µs. The converter-off ADC LED-readout image (`HV_TEST_ENABLE` 0) was flashed and verified on 2026-10-01, 2570 bytes. OFF DMM reading is 3.0517 V, first reported as 3.03 V. The HV_ADC node measured 8 mV. Nominal divider from 3.0517 V is 7.6 mV. Complete LED groups 1, 1, 1, 5 then 1, 1, 1, 6 decode to hv_adc_raw = 4 and hv_adc_avg = 5. Nominal HV from the average is 2.15 V. The 25 µs / 500 Hz correlation reads 56.8 V on the DMM. LED groups 1, 3, 7, 2 and 1, 3, 7, 2 decode to raw 261 and average 261. The ADC pin measured 280 mV, and count 261 is 280.4 mV at a 1.100 V reference. The nominal 40 MOhm / 100 kOhm divider predicts 142 mV and about 112 V from that count, so the fitted divider is still open. The earlier 25 µs DMM reading was 43.7 V. The 70 µs / 500 Hz correlation reads 106 V on the DMM. LED groups 1, 5, 9, 5 and 1, 5, 9, 6 decode to raw 484 and average 485. The ADC pin measured 519 mV, and count 484 is 519.9 mV at a 1.100 V reference. The nominal divider predicts 264 mV and about 209 V from the average. The earlier 70 µs DMM reading was 105 V. The first 100 µs correlation aborted at count 581 because the 250 V nominal ceiling latched PB1 low. The repeat, with the 350 V nominal ceiling, reads 133.7 V on the DMM. LED groups 1, 7, 2, 1 decode to raw 610 and average 610. The ADC pin measured 651.2 mV. Count 610 is 655 mV at a 1.100 V reference, so that pair puts the reference near 1.094 V. The nominal divider predicts 333 mV and about 263 V. The earlier 100 µs DMM reading was 133.5 V. The four DMM-connected ADC rows are recorded. The divider remains 401:1. A 60 V closed-loop image is built and not flashed. The Q1 collector waveform is still open. The tube stays disconnected.

Checkpoint results:
- Host toolchain: pass. `avr-gcc` 15.1.0 accepts `atmega328pb`. avrdude 8.1 has `m328pb` and `atmelice_isp`.
- Device signature: pass. `make read-signature` on 2026-10-01 returned `0x1e,0x95,0x16` (ATmega328PB). Recorded in `firmware/bringup/boards/board01.txt`.
- Fuse bytes: pass, read only. LFUSE `0x62`, HFUSE `0xD9`, EFUSE `0xF7`. Decoded against DS40001906C as the shipping clock: internal 8 MHz RC, CKDIV8 programmed, CPU clock 1.0 MHz. ISP enabled, RESET retained, debugWIRE off, BOD off, CFD off.
- Flash and verify: pass. 240 bytes, `F_CPU=1000000UL`, 2026-10-01.
- LED on PD7: pass. Toggles every 500 ms, consistent with the 1.0 MHz clock.
- PB1 held LOW: pass. The earlier high report was measurement error. Recorded in `firmware/bringup/boards/board01.txt`.
- 3.3 V rail: pass, measured 3.3 V.
- Supply current: pass, reported ok. No unexpected heating.
- HV converter: inactive. HV side measures 2.1 V.
- Phase 2: pass.
- Phase 3 fuse write: pass. 2026-10-01 read-back LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`, signature `0x1E9516`.
- Phase 4: pass. Pre-flash blink was exactly 8 Hz on the 1 MHz-timed image. Post-flash PD7 measured 500 Hz with the Timer2 image built at `F_CPU=8000000UL`. CPU clock is 8 MHz.
- Phase 5A LED: pass. 2026-10-01, PD7 blinks 500 ms on and 500 ms off. PB1 remains low.
- Phase 5B GM pulse: pass. 2026-10-01, falling edges on PD2 are counted and the LED flickers. Tube disconnected. PB1 remains low.
- Phase 5C HV ADC: pass. 2026-10-01, raw 5 with the converter off. Nominal HV-node voltage 2.15 V against an earlier 2.1 V meter reading.
- Phase 5D TWI: untested. No master was connected. Not a pass.
- HV characterization: closed, 2026-10-02. Nominal target 380 V. Nominal frequency about 2400–2500 Hz. Pulse cap 100 µs. Production abort around 400 V. Q1-diode peak at 380 V measured 427.4 V with the probe attached.
- Production hold: pass, 2026-10-02. 380 V at 2500 Hz. Q1-diode peak 427.4 V with the probe attached. Thermals OK. A meter on the HV output latches the hold off. Tube disconnected.
- Phase 7 HV open-loop: 25 µs PB1 checks pass. Scope: 499.44 Hz, 1.2563% duty, 25.155 µs high, 2.0022 ms period. HV output 43.7 V DC. 40 µs: 57.5 V DC. 70 µs at 500 Hz: 105 V DC. 70 µs at 800 Hz: 100.5 V DC. 85 µs at 500 Hz: 119.8 V DC. 100 µs cap at 500 Hz: 133.5 V DC. Width sweep finished. Do not exceed 100 µs. ADC correlation: OFF is 3.0517 V, HV_ADC 8 mV, raw 4, average 5. 25 µs is 56.8 V, HV_ADC 280 mV, raw 261, average 261. 70 µs is 106 V, HV_ADC 519 mV, raw 484, average 485. 100 µs is 133.7 V, HV_ADC 651.2 mV, raw 610, average 610, after a first attempt latched off at count 581. The 60 V closed-loop result is HV_TEST_OK: final ADC 143, max ADC 168, final 9 µs, max 20 µs, final estimate 61 V, max estimate 71 V. The 100 V result is HV_TEST_OK: final ADC 235, max ADC 259, final 21 µs, max 35 µs, final estimate 100 V, max estimate 111 V. The 150 V result is HV_TEST_OK: final ADC 348, max ADC 376, final 41 µs, max 56 µs, final estimate 149 V, max estimate 160 V. The 200 V result is HV_TEST_OK: final ADC 463, max ADC 488, final 65 µs, max 78 µs, final estimate 198 V, max estimate 209 V. The 250 V result is HV_TEST_OK: final ADC 580, max ADC 596, final 92 µs, max 100 µs, final estimate 248 V, max estimate 255 V. The frequency series on 2026-10-01 is COMPLETE: 275 V at 600 Hz is HV_TEST_OK, final ADC 643, max 648, final 97 µs, max 100 µs, 180 ms at the cap, final estimate 275 V. 300 V at 600 Hz is HV_TEST_SATURATED at 100 µs, final ADC 655, about 281 V. 300 V at 750 Hz is HV_TEST_OK, final ADC 697, final 97 µs, max 100 µs, final estimate 299 V. 300 V at 1000 Hz is HV_TEST_OK, final ADC 698, final 83 µs, max 89 µs, final estimate 299 V, and it did not reach the cap. The upper-300 ladder at 1000 Hz is COMPLETE: 310 V final ADC 724, final 88 µs, max 94 µs; 320 V final ADC 745, final 92 µs, max 98 µs; 330 V final ADC 770, final 97 µs, max 100 µs, 100 ms at the cap, final estimate 330 V, max estimate 333 V. All three are HV_TEST_OK. Later series did run 340 V and above. Q1 at 330 V measured about 360 V. Q1 at the 380 V / 1500 Hz point measured 411 V.

Decisions that must survive across sessions:
- Build with `/opt/avr8-gnu-toolchain-4.0.0/bin/avr-gcc`, `avr-objcopy`, and `avr-size`. The `avr-gcc` 12.1.0 on `PATH` is a different install.
- `avrdude` stays `/usr/bin/avrdude` 8.1. It is not in the toolchain tree.
- Saved fuses select the internal 8 MHz RC with CKDIV8 unprogrammed. The measured PD7 frequency is 500 Hz, and the Makefile default is `F_CPU=8000000UL`.
- The HV divider is 40 MOhm / 100 kOhm, ratio 401:1. Do not infer a different ratio from DMM readings taken at different times.
- The temporary bring-up ADC reference is 1095 mV. It is not the final calibration. Closed-loop counts use divisor 1023.
- Characterization limits are frozen as of 2026-10-02. Nominal tube target is 380 V. Nominal frequency is about 2400–2500 Hz. The absolute pulse cap is 100 µs. The production HV abort is around 400 V, which is 932 counts with the 401:1 divider, 1095 mV reference, and divisor 1023. That abort is not the 995-count characterization ceiling. The Q1-diode peak at 380 V measured 427.4 V with the probe attached. Do not raise the target, the frequency band, or the pulse cap without a new decision. Do not change fuses.
- The first closed-loop image targets 60 V at 500 Hz, starts at 10 µs, and hard-aborts at ADC count 580. The pulse cap remains 100 µs and the run remains 10 s. The 60 V result on 2026-10-01 is HV_TEST_OK: final ADC 143, max ADC 168, final pulse 9 µs, max pulse 20 µs, final estimate 61 V, max estimate 71 V. The 100 V result the same day is HV_TEST_OK: final ADC 235, max ADC 259, final pulse 21 µs, max pulse 35 µs, final estimate 100 V, max estimate 111 V. The 150 V result is HV_TEST_OK: final ADC 348, max ADC 376, final pulse 41 µs, max pulse 56 µs, final estimate 149 V, max estimate 160 V. The 200 V result is HV_TEST_OK: final ADC 463, max ADC 488, final pulse 65 µs, max pulse 78 µs, final estimate 198 V, max estimate 209 V. The abort is now 815 counts, about 350 V. The 250 V result is HV_TEST_OK: final ADC 580, max ADC 596, final pulse 92 µs, max pulse 100 µs, final estimate 248 V, max estimate 255 V. The climb used the 100 µs cap. The later frequency series did run 300 V at 600, 750, and 1000 Hz. It did not run 300 V at 500 Hz. The upper-300 ladder at 1000 Hz is also COMPLETE: 310 V, 320 V, and 330 V are HV_TEST_OK. 330 V held 97 µs and touched 100 µs. Q1 collector at that 330 V point measured about 360 V. The 1200 Hz series is COMPLETE. 340 V is HV_TEST_OK at a 99 µs hold. 350 V, 360 V, and 380 V all saturate at 100 µs and about 342 V. One Q1 collector reading during that series was 383.77 V. The later series is also COMPLETE: 360 V at 1400 Hz held 93 µs, and 380 V at 1500 Hz held 99 µs. 1600 Hz was not run. Q1 at the 380 V / 1500 Hz point measured 411 V. The 2000 Hz ladder is COMPLETE. 350 V through 380 V held below the 100 µs cap. 390 V sat at 100 µs and about 389 V and was still HV_TEST_OK. 400 V saturated at the same ceiling, about 389 V. Scope peaks from GND to the Q1-diode net were 395 V, 407 V, 415 V, 427 V, 439 V, and 439 V. The 3.3 V rail stayed stable. Ringing was present, well behaved, and fell off fast. Temperature was OK. The 380 V sweep from 2000 Hz to 2500 Hz is COMPLETE. Holds were 96, 95, 94, 92, 91, and 90 µs. Q1-diode peaks were 423 V, then 427 V. The 2500 Hz ladder from 350 V to 400 V is COMPLETE. 400 V held 97 µs, peak reservoir 403 V. Q1-diode peaks were 391 V, 403 V, 415 V, 427 V, 435 V, and 447 V. The 400 V sweep from 2500 Hz to 3000 Hz is COMPLETE. Holds were 85, 84, 95, 94, 93, and 92 µs. All six are HV_TEST_OK. The Q1-diode peak was 447 V on every step. Ringing subsided well in time. The 3.3 V rail stayed stable. Temperature was OK.

Open blockers:
- TWI is untested. Do not treat `0x66` as verified until it is read back. The GM tube stays disconnected. The 100 µs pulse-width cap stays. The 60 V through 250 V closed-loop results are HV_TEST_OK. The frequency series is COMPLETE in EEPROM. 600 Hz regulates 275 V but saturates at 300 V near 281 V. 750 Hz reaches 300 V with a 97 µs hold. 1000 Hz holds 300 V at 83 µs. The same frequency holds 310 V at 88 µs, 320 V at 92 µs, and 330 V at 97 µs. The 330 V climb touched the 100 µs cap. The earlier abort of 815 counts applied through the 1000 Hz ladder. Q1 collector at about 330 V measured about 360 V on the STR1550 (VCEO 500 V), so the margin at that point is about 140 V. 360 V at 1400 Hz and 380 V at 1500 Hz are HV_TEST_OK. 1600 Hz was not needed. The pulse cap stays 100 µs. Q1 at the 380 V / 1500 Hz point measured 411 V. The 2000 Hz ladder is COMPLETE in EEPROM. 380 V held 96 µs. 390 V and 400 V both sit at about 389 V and 100 µs. 400 V is HV_TEST_SATURATED. The unloaded ceiling at 2000 Hz and 100 µs is about 389 V. The pulse cap stays 100 µs. The hard abort stays 995 counts. Scope peaks from GND to the Q1-diode net were 395 V, 407 V, 415 V, 427 V, 439 V, and 439 V. The highest is 11 V under the 450 V stop. The 3.3 V rail stayed stable. Ringing was present, well behaved, and fell off fast. Temperature was OK. The 380 V sweep from 2000 Hz to 2500 Hz is COMPLETE. Holds were 96, 95, 94, 92, 91, and 90 µs. All six are HV_TEST_OK. The 2200 Hz step touched 100 µs for 260 ms and peaked near 391 V, then settled at 94 µs. Q1-diode peaks on that sweep were 423 V at 2000 Hz and 427 V from 2100 Hz through 2500 Hz. The 2500 Hz ladder from 350 V to 400 V is COMPLETE and every step is HV_TEST_OK. 400 V held 97 µs with a 99 µs peak and a 403 V reservoir peak. Q1-diode peaks on that ladder were 391 V, 403 V, 415 V, 427 V, 435 V, and 447 V. The 3.3 V rail stayed stable. Ringing was present, well behaved, and fell off fast. Temperature was OK. 447 V is 3 V under the 450 V stop. The 400 V sweep from 2500 Hz to 3000 Hz is COMPLETE. Holds were 85, 84, 95, 94, 93, and 92 µs. All six are HV_TEST_OK. The Q1-diode peak was 447 V on every step. Ringing subsided well in time. The 3.3 V rail stayed stable. Temperature was OK. Characterization is frozen: nominal target 380 V, nominal frequency about 2400–2500 Hz, pulse cap 100 µs, production abort around 400 V. The 995-count abort was characterization only. Do not change fuses.

======================================================================
1. HARDWARE
======================================================================

MCU:
- Microchip ATmega328PB-AU
- TQFP-32
- Nominal MCU supply: 3.3 V
- Final intended CPU clock: internal 8 MHz RC oscillator
- No external crystal
- No bootloader
- Programming: AVR ISP
- Programmer: Atmel-ICE

IMPORTANT:
- This is ATmega328PB, NOT ATmega328P.
- Do not substitute ATmega328P device definitions.
- Check all registers, interrupt vectors, fuse definitions, and peripheral names against ATmega328PB support in the installed toolchain and datasheet.

Programming connection:
- MOSI0
- MISO0
- SCK0
- RESET
- VCC sense
- GND

The PCB is externally powered.
Do NOT assume Atmel-ICE powers the target.

======================================================================
2. CURRENT BOARD PIN ASSIGNMENT
======================================================================

ATmega328PB:

PB1 / OC1A:
    HV_SW
    Drives the high-voltage converter transistor.

PC0 / ADC0:
    HV_ADC
    High-voltage feedback measurement.

PD2 / INT0:
    GM_PULSE
    Active-low Geiger pulse input.

PD7:
    LED

PC4 / SDA / TWI0:
    SDA

PC5 / SCL / TWI0:
    SCL

PD3:
    UNUSED / SPARE GPIO
    Do not initialize or depend on it.

SPI0 / ISP:
    MOSI0
    MISO0
    SCK0
    RESET

THERE IS NO BUZZER ON THE CURRENT BOARD REVISION.

Do not create:
- buzzer.c
- buzzer.h
- buzzer tests
- OC2B buzzer configuration
- PD3 buzzer handling

Timer2 is currently free.

======================================================================
3. POWER ARCHITECTURE
======================================================================

External Grove VCC may be approximately:
- 3.3 V
- 5 V

The board uses a 3.3 V LDO for the internal rail.

Internal 3.3 V powers:
- ATmega328PB
- BME680
- I2C low-voltage side
- HV converter input

The board contains an onboard BME680.

There are BSS138 bidirectional I2C level shifters between:

    SDA_HOST / SCL_HOST
            |
         BSS138
            |
       SDA / SCL

Internal I2C pull-ups:
- 4.7 kOhm
- to +3.3 V

HOST-side pull-ups may be supplied by the Seeed Solar Node.

The onboard BME680 can be disconnected from the internal SDA/SCL bus using two normally-closed solder jumpers.

The ATmega firmware does NOT need to control the BME680.

The external Meshtastic host can access:
- GeigerNode ATmega at 0x66
- onboard BME680 directly

======================================================================
4. HIGH-VOLTAGE SAFETY
======================================================================

THIS BOARD GENERATES APPROXIMATELY 400 V.

Treat the HV subsystem as safety-critical.

HV_SW = PB1 / OC1A.

During initial bring-up:

    HV MUST NEVER BE ENABLED.

Requirements:

- PB1 must be configured as output LOW as the first hardware action in firmware.
- Timer1 PWM must remain disabled.
- OC1A output mode must remain disconnected.
- No code may toggle PB1 during Phase 1.
- No generic GPIO test may touch PB1.
- No peripheral initialization may take ownership of PB1 accidentally.

There is hardware biasing intended to keep the transistor OFF, but firmware must still explicitly maintain HV_SW LOW.

Never implement "test all GPIO pins" code that includes PB1.

Later HV firmware must independently enforce:
- maximum pulse width
- maximum repetition frequency
- maximum duty cycle
- maximum HV ADC reading
- startup timeout
- fault state
- watchdog-safe shutdown
- command-off state
- reset-safe state

A feedback algorithm must NEVER be the only thing preventing continuous Q1 drive.

======================================================================
5. GM INPUT
======================================================================

GM_PULSE:
- PD2 / INT0
- active LOW
- falling-edge interrupt

Later implementation:

ISR:
    increment pulse counter
    return

ISR must NOT:
- calculate CPM
- calculate radiation
- blink LEDs
- perform I2C
- delay
- print/debug
- perform floating-point math

Use a volatile counter.

Any multi-byte counter accessed from both main and ISR must be accessed atomically.

======================================================================
6. HV ADC
======================================================================

HV_ADC:
- PC0 / ADC0

HV feedback divider is approximately:

    HV
     |
    40 MOhm
     |
    HV_ADC
     |
    100 kOhm
     |
    GND

At approximately 400 V:
    HV_ADC is approximately 1.0 V

Intended ADC reference:
    internal nominal ~1.1 V reference

ADC calibration must eventually use measured hardware values rather than assuming the internal reference is exactly 1.100 V.

During initial ADC testing:

    HV converter remains OFF.

======================================================================
7. I2C / RADSENS COMPATIBILITY
======================================================================

The ATmega328PB operates as an I2C/TWI SLAVE.

Target I2C address:

    0x66

The firmware must eventually emulate enough of a ClimateGuard RadSens interface for Meshtastic.

Minimum required functionality:

Register 0x00:
    return 0x7D

Registers 0x06..0x08:
    24-bit big-endian static radiation value

Unit:
    0.1 uR/h

Protocol behavior must support:

1. master START
2. slave address + WRITE
3. register pointer byte
4. repeated START
5. slave address + READ
6. sequential register reads
7. STOP

Do not assume every master inserts STOP between register selection and read.

Future extended register map:

0x00       device ID
0x01       firmware version
0x03..05   dynamic radiation
0x06..08   static radiation
0x09..0A   pulse count
0x0C       low-power control
0x10       address
0x11       HV enable
0x12..13   sensitivity
0x14       LED control

Do not implement the full map during first bring-up.

======================================================================
8. TOOLCHAIN
======================================================================

Use:

- Microchip AVR 8-bit GNU Toolchain 4.0.0
- avr-gcc
- avr-libc
- avr-objcopy
- avr-size
- GNU Make
- avrdude
- Atmel-ICE ISP

NO:
- Arduino
- PlatformIO
- Arduino core
- MiniCore
- bootloader
- vendor HAL
- dynamic framework initialization

Programming transport:

    Atmel-ICE ISP

Compiler MCU:

    atmega328pb

avrdude MCU identifier is expected to be:

    m328pb

Programmer identifier is expected to be:

    atmelice_isp

If the locally installed avrdude uses different identifiers:
- inspect avrdude's supported devices/programmers
- report the discrepancy
- do NOT silently substitute ATmega328P.

======================================================================
9. VERY IMPORTANT FUSE SAFETY POLICY
======================================================================

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
FUSE PROGRAMMING WARNING
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

DO NOT WRITE ANY FUSES DURING INITIAL BRING-UP.

DO NOT GUESS FUSE BYTES.

DO NOT COPY ATmega328P FUSE VALUES FROM INTERNET EXAMPLES.

DO NOT HAVE "make flash" MODIFY FUSES.

DO NOT HAVE ANY NORMAL BUILD OR PROGRAMMING COMMAND MODIFY FUSES.

DO NOT AUTOMATICALLY EXECUTE FUSE PROGRAMMING COMMANDS.

Before ANY fuse write:

1. Read the existing device signature.
2. Read LFUSE.
3. Read HFUSE.
4. Read EFUSE.
5. Save the original values.
6. Decode the original values against the ATmega328PB datasheet.
7. Determine the actual current clock source.
8. Determine whether CKDIV8 is enabled.
9. Present proposed new values.
10. Decode EVERY relevant proposed bit.
11. Have a human explicitly approve the values.
12. Only then may a separate manual programming command be prepared.

Critical fuse bits include, but are not limited to:

- CKDIV8
- CKOUT
- CKSEL
- SUT
- SPIEN
- DWEN
- RSTDISBL
- WDTON
- EESAVE
- BOOTSZ
- BOOTRST
- BODLEVEL

EXTRA WARNING:

SPIEN and RSTDISBL affect ISP accessibility.

Incorrect values can make the MCU inaccessible using the normal Atmel-ICE ISP connection.

Incorrect CKSEL settings can also make the MCU appear dead because it may expect an external clock source.

Do not change:
- SPIEN
- RSTDISBL
- DWEN

without a very specific reason and explicit human authorization.

For this project:
- ISP must remain enabled
- RESET must remain RESET
- debugWIRE must remain disabled
- internal RC oscillator must ultimately be used

The final target is:

    internal 8 MHz oscillator
    CKDIV8 disabled

BUT:

DO NOT ASSUME THE MCU CURRENTLY RUNS AT 8 MHz.

Factory/new MCU configuration may have CKDIV8 enabled.

Therefore the initial firmware's timing must be based on the ACTUAL fuse state discovered during bring-up.

Do not program final clock fuses merely because the Makefile says F_CPU=8000000.

F_CPU is a compiler assumption.
Fuse bits determine the actual hardware clock.

Those two values must agree.

======================================================================
10. REPOSITORY STRUCTURE
======================================================================

Create:

firmware/
    Makefile
    README.md

    include/
        board.h
        gpio.h
        adc.h
        gm_counter.h
        twi_slave.h
        radsens.h
        hv.h
        watchdog.h

    src/
        main.c
        gpio.c
        adc.c
        gm_counter.c
        twi_slave.c
        radsens.c
        hv.c
        watchdog.c

    bringup/
        README.md
        boards/
            .gitkeep

Do not create buzzer files.

For the first task only these need real implementation:

    Makefile
    README.md
    bringup/README.md
    include/board.h
    src/main.c

Other modules may be absent or empty placeholders.

Prefer NOT creating unused placeholders unless they improve repository organization.

======================================================================
11. MAKEFILE
======================================================================

Use a readable hand-written Makefile.

Expected variables:

    TARGET := geigernode
    MCU := atmega328pb
    AVRDUDE_MCU := m328pb

    CC := avr-gcc
    OBJCOPY := avr-objcopy
    SIZE := avr-size
    AVRDUDE := avrdude

    PROGRAMMER := atmelice_isp
    PORT := usb

F_CPU must NOT initially be blindly fixed to 8 MHz.

Provide an explicit Make variable, for example:

    F_CPU ?= 1000000UL

but clearly document that its value MUST be changed to match the fuse-derived actual CPU clock.

Alternatively require:

    make F_CPU=1000000UL

or

    make F_CPU=8000000UL

Do not silently hide this choice.

After final fuse configuration is approved and installed, the project can default to:

    F_CPU := 8000000UL

Recommended flags:

    -mmcu=$(MCU)
    -DF_CPU=$(F_CPU)
    -Os
    -std=gnu11
    -Wall
    -Wextra
    -Wshadow
    -Wundef
    -ffunction-sections
    -fdata-sections

Linker:

    -mmcu=$(MCU)
    -Wl,--gc-sections

Do not add complex optimization flags during first bring-up.

Required targets:

    make
    make clean
    make size
    make flash
    make verify
    make read-signature
    make read-fuses
    make toolchain-info

Optional useful target:

    make disasm

which creates a human-readable AVR disassembly.

======================================================================
12. MAKEFILE FUSE PROTECTION
======================================================================

`make read-fuses` only reads.

`make fuses` may write only the Phase 3 bytes approved on 2026-10-01:

    LFUSE = 0xE2
    HFUSE = 0xD9
    EFUSE = 0xF5

It must not program flash. Any other fuse value needs a new decode and a new explicit approval before the Makefile changes.

Example semantics:

    avrdude \
        -p $(AVRDUDE_MCU) \
        -c $(PROGRAMMER) \
        -P $(PORT) \
        -U lfuse:r:-:h \
        -U hfuse:r:-:h \
        -U efuse:r:-:h

Never combine fuse operations with flash programming.

======================================================================
13. PHASE 0 — TOOLCHAIN VALIDATION
======================================================================

Before compiling firmware, document how to perform:

    avr-gcc --version
    avr-objcopy --version
    avr-size --version
    avrdude -v

Verify that avr-gcc supports:

    -mmcu=atmega328pb

Example test:

    echo | avr-gcc -mmcu=atmega328pb -dM -E -

Verify Atmel-ICE communication.

Read the device signature.

Do NOT write flash yet if the device signature does not match ATmega328PB.

Read all fuse bytes.

Record:

- device signature
- LFUSE
- HFUSE
- EFUSE
- avrdude version
- avr-gcc version
- board identifier / serial if available
- measured VCC

Suggested bring-up record:

    bringup/boards/board01.txt

Do not modify fuse values.

======================================================================
14. PHASE 1 — PHYSICAL POWER CHECK
======================================================================

Before firmware programming:

1. Visual inspection.
2. Check for solder bridges.
3. Check ATmega orientation.
4. Check regulator orientation.
5. Check Q1 / D1 orientation.
6. Check 3.3 V to GND resistance.
7. Check raw VCC to GND resistance.
8. Check ISP continuity.
9. Power board from a current-limited bench supply.
10. Verify approximately 3.3 V regulator output.
11. Verify ATmega VCC and AVCC.
12. Verify BME680 supply.
13. Check that no component heats unexpectedly.
14. Check PB1 / HV_SW is not being driven.

HV remains disabled.

Do not install or energize the HV section merely to test MCU firmware.

======================================================================
15. PHASE 2 — MINIMAL MCU SANITY FIRMWARE
======================================================================

Implement the smallest useful firmware.

main() must begin with:

1. PB1 output LOW
2. LED PD7 output configuration

Conceptually:

    int main(void)
    {
        /* Safety first: HV converter OFF */
        PORTB &= ~_BV(PORTB1);
        DDRB  |=  _BV(DDB1);

        /* LED */
        DDRD |= _BV(DDD7);

        for (;;) {
            PORTD ^= _BV(PORTD7);
            _delay_ms(500);
        }
    }

Pay attention to AVR GPIO sequencing.

The PB1 output latch should be LOW before or at the same time PB1 becomes an output.

Do not configure Timer1.

Do not configure TWI.

Do not configure ADC.

Do not configure INT0.

Do not touch PD3.

Do not enable HV.

Do not write fuses.

======================================================================
16. INITIAL CLOCK ISSUE
======================================================================

The LED delay is not considered proof of an 8 MHz clock unless fuse configuration is known.

If the MCU is running at 1 MHz because CKDIV8 is enabled while firmware was compiled with:

    F_CPU=8000000UL

then delay timing will be wrong by approximately 8x.

Therefore:

1. Read fuses FIRST.
2. Decode clock configuration.
3. Compile Phase 2 with F_CPU matching the actual clock.
4. Flash LED firmware.
5. Measure behavior.
6. Later change fuses only after explicit review.

======================================================================
17. PHASE 2 SUCCESS CRITERIA
======================================================================

Do not proceed unless:

- Atmel-ICE detects target
- ATmega328PB device signature is correct
- fuse bytes can be read
- original fuse values are saved
- build succeeds with no important warnings
- flash succeeds
- verify succeeds
- LED blinks
- PB1 measures LOW
- HV converter remains inactive
- supply current is reasonable
- no component heats unexpectedly

======================================================================
18. PHASE 3 — CLOCK AND FUSE REVIEW
======================================================================

DO NOT automatically perform this phase.

After Phase 2 is confirmed:

Determine desired final configuration:

    internal RC oscillator
    8 MHz CPU clock
    CKDIV8 disabled
    ISP retained
    RESET retained
    debugWIRE disabled

Propose a suitable BOD configuration for a nominal 3.3 V rail.

Do not assume a particular BOD fuse value without checking the ATmega328PB datasheet.

Prepare a report:

CURRENT:
    LFUSE = ...
    HFUSE = ...
    EFUSE = ...

Decode each implemented bit.

PROPOSED:
    LFUSE = ...
    HFUSE = ...
    EFUSE = ...

Decode each implemented bit.

Explicitly highlight any changed bit.

If SPIEN, RSTDISBL, DWEN or clock-source selection would change unexpectedly:

    STOP.

Do not produce an executable fuse command until a human explicitly says the proposed fuse configuration is approved.

======================================================================
19. PHASE 4 — CLOCK VERIFICATION
======================================================================

Before and after any approved fuse change:

Generate a known frequency using hardware timer output or GPIO timing.

Measure it with:
- oscilloscope
or
- logic analyzer

Do not infer clock frequency only from LED blinking.

After final configuration verify:

    actual CPU clock ≈ 8 MHz

Then set normal project default:

    F_CPU = 8000000UL

======================================================================
20. PHASE 5 — LOW-VOLTAGE PERIPHERAL BRING-UP
======================================================================

Bring up one peripheral at a time:

A. LED

B. GM_PULSE / INT0

C. ADC0 / HV_ADC with HV OFF

D. TWI0 slave

No buzzer test exists.

PD3 remains spare.

======================================================================
21. GM_PULSE TEST
======================================================================

Configure:

    PD2 / INT0
    falling-edge trigger

ISR:

    ISR(INT0_vect)
    {
        gm_pulse_count++;
    }

Use an appropriate volatile counter.

Do not perform application work inside ISR.

Main code may atomically copy and inspect the pulse count.

Provide a way to inject a low-going test pulse before connecting/depending on an actual GM tube signal.

======================================================================
22. ADC TEST
======================================================================

Configure ADC0.

Reference:
    internal nominal 1.1 V

HV remains OFF.

Provide:

    uint16_t adc_read_hv_raw(void);

and pure conversion helpers such as:

    uint32_t adc_raw_to_uv(uint16_t raw, uint32_t vref_uv);
    uint32_t hv_adc_to_voltage(...);

Do not use floating point unless there is a strong reason.

Use fixed-point/integer arithmetic.

Keep these constants centralized:

- ADC resolution
- nominal Vref
- measured/calibrated Vref
- high-side divider resistance
- low-side divider resistance

======================================================================
23. TWI SLAVE TEST
======================================================================

Configure TWI0 as slave:

    address 0x66

Initial register implementation:

    0x00 -> 0x7D

and:

    0x06
    0x07
    0x08

return a fixed synthetic radiation value.

Implement a small register pointer protocol.

Required:
- write register address
- repeated START
- read
- sequential reads
- STOP handling

Do not allocate memory dynamically.

Do not perform slow application work inside the TWI ISR.

======================================================================
24. I2C HARDWARE TEST
======================================================================

Test with the real Seeed Solar Node / Grove connection.

Measure both sides of the BSS138 translators.

HOST:
    SDA_HOST
    SCL_HOST

INTERNAL:
    SDA
    SCL

Verify:
- internal HIGH ≈ 3.3 V
- actual HOST HIGH voltage
- proper LOW levels
- sensible rise times
- no bus contention

Run I2C scan.

Expected devices include:
- GeigerNode at 0x66
- onboard BME680 at its strapped address

Then test BME680 isolation jumpers:

1. onboard sensor connected:
   BME680 should appear.

2. open both BME680 SDA/SCL jumpers:
   onboard BME680 should disappear.

3. GeigerNode ATmega 0x66 must remain functional.

4. external sensor on AUX should remain usable.

======================================================================
25. PHASE 6 — RADSENS EMULATION
======================================================================

Only after TWI is stable.

Implement a register bank.

Minimum:

    0x00 = 0x7D

Synthetic static radiation:

    0x06..0x08

Use an atomic/coherent snapshot.

Never allow a three-byte read to combine bytes from two different measurement updates.

Example:

    uint32_t static_radiation_x10;

When publishing:
- build all three register bytes from one snapshot
- make register update atomic relative to TWI access

======================================================================
26. PHASE 7 — HV OPEN-LOOP BRING-UP
======================================================================

DO NOT ENABLE UNTIL EXPLICITLY REQUESTED.

HV hardware:
- PB1 / OC1A
- Timer1

Initial test must be open-loop.

Start conservatively.

Suggested initial characterization region:

    pulse width: approximately 20–30 us
    repetition: approximately 500–1000 Hz

During Rev-A characterization:

    hard maximum pulse width <= 100 us

This maximum must exist independently from feedback regulation.

Procedure:

1. Keep HV power stage disabled if possible.
2. Configure Timer1.
3. Scope PB1.
4. Verify pulse width.
5. Verify period.
6. Verify reset returns PB1 LOW.
7. Verify firmware fault returns PB1 LOW.
8. Current-limit board supply.
9. Energize converter.
10. Use proper HV probe.
11. Measure Q1 collector waveform.
12. Check ringing/overshoot.
13. Measure actual HV.
14. Compare ADC feedback.

Increase HV progressively:

    ~100 V
    ~200 V
    ~300 V
    upper-300 V region

Do NOT immediately command 400 V.

Characterize the hardware before implementing closed-loop control.

======================================================================
27. PHASE 8 — HV CLOSED LOOP
======================================================================

After open-loop characterization.

Use explicit state machine:

    HV_OFF
    HV_STARTING
    HV_REGULATING
    HV_FAULT

Every transition must be understandable.

HV_FAULT must force:

    PB1 LOW
    Timer1 output disconnected

Possible fault conditions:

- ADC overvoltage
- invalid ADC value
- startup timeout
- excessive drive demand
- watchdog/reset
- explicit OFF command

Add hard limits independent of regulator output.

For example:

    requested_on_time =
        min(regulator_requested_on_time,
            HARD_MAX_ON_TIME);

Never allow regulator wind-up to result in continuous drive.

======================================================================
28. PHASE 9 — REAL GM MEASUREMENT
======================================================================

After HV is stable.

INT0 counts pulses.

Maintain:
- total pulse count
- measurement-window pulse count
- CPM
- dynamic estimate
- static estimate

Use configurable tube sensitivity coefficient:

    S = impulses / uR

Conversion:

    uR/h = CPM * 60 / S

RadSens representation:

    raw = uR/h * 10

Store:

    reg[0x06] = (raw >> 16) & 0xff;
    reg[0x07] = (raw >> 8)  & 0xff;
    reg[0x08] = raw & 0xff;

Use fixed-point arithmetic where practical.

Do not permanently bake calibration into unrelated source files.

Keep calibration constants together.

======================================================================
29. PRODUCTION ARCHITECTURE
======================================================================

Use pure C.

No:
- malloc
- calloc
- realloc
- free
- floating runtime dependency unless justified
- C++ runtime
- Arduino
- framework callbacks

Suggested final modules:

include/
    board.h
    gpio.h
    adc.h
    gm_counter.h
    twi_slave.h
    radsens.h
    hv.h
    watchdog.h
    radiation.h

src/
    main.c
    gpio.c
    adc.c
    gm_counter.c
    twi_slave.c
    radsens.c
    hv.c
    watchdog.c
    radiation.c

Timer allocation target:

Timer1:
    HV pulse generation

Timer2:
    available for system timing / scheduler if useful

INT0:
    GM pulses

TWI0:
    RadSens slave

ADC0:
    HV feedback

======================================================================
30. MAIN LOOP DESIGN
======================================================================

Target architecture:

INT0 ISR:
    increment GM count
    return

TWI ISR:
    advance transaction state
    return

timer ISR:
    minimal scheduling counters only
    return

main:
    process elapsed-time flags
    copy GM counts atomically
    calculate CPM
    calculate radiation
    update RadSens register snapshot
    service HV regulator
    service fault state
    service LED
    reset watchdog

Avoid blocking code in production.

_delay_ms() is acceptable only for early LED bring-up.

======================================================================
31. WATCHDOG
======================================================================

Do not enable watchdog blindly during Phase 1.

First inspect:
- reset cause
- WDTON fuse
- existing watchdog state

Production firmware should eventually use watchdog.

On startup:
- capture/reset MCUSR appropriately
- establish safe HV state
- configure watchdog intentionally

Any watchdog reset must result in HV OFF.

======================================================================
32. CODE QUALITY
======================================================================

Prefer:
- small modules
- explicit register operations
- explicit integer types
- uint8_t / uint16_t / uint32_t
- static functions where appropriate
- const data
- simple state machines
- compile-time constants
- comments explaining hardware reasons

Avoid:
- unnecessary macros
- clever metaprogramming
- magic numbers
- duplicated register configuration
- huge ISRs
- global mutable state without reason

Use:
    <avr/io.h>
    <avr/interrupt.h>
    <avr/atomic.h>
    <avr/wdt.h>
    <util/delay.h>

where appropriate.

======================================================================
33. FIRST CURSOR TASK
======================================================================

DO NOT implement the full firmware yet.

For the first response, create ONLY:

    Makefile
    README.md
    bringup/README.md
    include/board.h
    src/main.c

The firmware must:

1. target ATmega328PB
2. build using avr-gcc
3. program with Atmel-ICE ISP
4. provide read-signature command
5. provide read-fuses command
6. NEVER write fuses
7. NEVER enable HV
8. force PB1 LOW
9. blink LED PD7
10. leave PD3 unused
11. leave Timer1 disabled
12. leave Timer2 disabled
13. leave TWI disabled
14. leave ADC disabled
15. leave INT0 disabled

Do not create buzzer functionality.

======================================================================
34. FIRST RESPONSE FORMAT
======================================================================

Show the complete contents of:

1. Makefile
2. include/board.h
3. src/main.c
4. README.md
5. bringup/README.md

Then provide the exact human bring-up procedure:

1. inspect PCB
2. resistance checks
3. connect current-limited external supply
4. verify 3.3 V
5. verify no unexpected heating
6. connect Atmel-ICE
7. read device signature
8. read fuse bytes
9. SAVE fuse bytes
10. decode current clock configuration
11. select matching F_CPU
12. build
13. inspect avr-size
14. flash
15. verify
16. observe LED
17. scope/measure PB1
18. confirm PB1 remains LOW
19. record results

STOP THERE.

Do not proceed to:
- fuse writing
- I2C
- ADC
- GM interrupt
- HV PWM
- closed-loop regulation

until the user provides the measured bring-up results.

======================================================================
35. FINAL FUSE REMINDER
======================================================================

FUSE VALUES ARE NOT ORDINARY FIRMWARE CONFIGURATION.

An incorrect fuse write can:
- select the wrong clock source
- disable ISP
- disable RESET
- enable unwanted debug behavior
- make the MCU appear dead

Therefore:

READ -> SAVE -> DECODE -> PROPOSE -> REVIEW -> HUMAN APPROVAL -> WRITE

Never:

GUESS -> WRITE