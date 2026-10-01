# GeigerNode Firmware — Master Development Prompt

You are developing firmware and performing hardware bring-up for a custom PCB called GeigerNode.

The firmware must be written for bare AVR silicon using the GNU AVR toolchain. Do not introduce Arduino, PlatformIO, vendor HALs, bootloaders, or unnecessary abstraction layers.

The immediate goal is SAFE HARDWARE BRING-UP of newly manufactured PCBs using an Atmel-ICE programmer.

Do not jump ahead to the complete firmware. Work through the bring-up phases in order and stop at explicit checkpoints.

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

There must be NO normal fuse-writing target.

If a target named:

    make fuses

exists, it must FAIL intentionally and print something like:

    ERROR: Fuse programming is intentionally disabled.
    Read and review ATmega328PB fuse values first.

DO NOT put a fuse-writing avrdude command behind that target.

The only fuse-related target initially allowed is:

    make read-fuses

It should only READ.

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