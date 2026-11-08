# GeigerNode Phase 2 bring-up.
#
# F_CPU is a compiler assumption. Fuse bits set the real CPU clock.
# Read and save the fuse bytes, decode the clock, then build with a
# matching value before flashing:
#
#   make F_CPU=1000000UL
#   make F_CPU=8000000UL
#
# Board 01 measured 500 Hz on PD7 with this image, so the CPU clock is 8 MHz.
# The default matches that clock.

TOOLCHAIN ?= /opt/avr8-gnu-toolchain-4.0.0
ifeq ($(wildcard $(TOOLCHAIN)/bin/avr-gcc),)
TOOLCHAIN := /opt/avr-gcc-15.2.0
endif

TARGET := geigernode
MCU := atmega328pb
AVRDUDE_MCU := m328pb

CC := $(TOOLCHAIN)/bin/avr-gcc
OBJCOPY := $(TOOLCHAIN)/bin/avr-objcopy
OBJDUMP := $(TOOLCHAIN)/bin/avr-objdump
SIZE := $(TOOLCHAIN)/bin/avr-size
AVRDUDE := avrdude
AVRDUDE_CONF ?= /etc/avrdude.conf

# Flipper AVR Flasher speaks STK500v1 on CDC if02. CLI is if00; do not use it.
# Atmel-ICE: make PROGRAMMER=atmelice_isp PORT=usb
PROGRAMMER ?= stk500v1
PORT ?= $(firstword $(wildcard /dev/serial/by-id/usb-Flipper_Devices_Inc.*-if02))
AVRDUDE_BAUD ?= 115200

F_CPU ?= 8000000UL

# Choose the I2C map at compile time: PROTOCOL=native (0x67) or
# PROTOCOL=radsens (0x66). math-test, clean, and toolchain-info do not
# need it.
NEED_PROTOCOL := 1
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out math-test clean toolchain-info,$(MAKECMDGOALS)),)
NEED_PROTOCOL :=
endif
endif
ifdef NEED_PROTOCOL
ifndef PROTOCOL
$(error Set PROTOCOL=native (0x67) or PROTOCOL=radsens (0x66))
endif
ifeq ($(filter $(PROTOCOL),native radsens),)
$(error PROTOCOL must be native or radsens)
endif
endif

CFLAGS := \
	-mmcu=$(MCU) \
	-DF_CPU=$(F_CPU) \
	-Os \
	-std=gnu11 \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wundef \
	-ffunction-sections \
	-fdata-sections \
	-Iinclude

ifeq ($(PROTOCOL),native)
CFLAGS += -DGEIGERNODE_PROTOCOL_NATIVE
endif

# CI sets EXTRA_CFLAGS=-Werror. A bench build leaves warnings as warnings.
EXTRA_CFLAGS ?=
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS := -mmcu=$(MCU) -Wl,--gc-sections

AVRDUDE_FLAGS := -C $(AVRDUDE_CONF) -p $(AVRDUDE_MCU) -c $(PROGRAMMER) -P $(PORT)
ifeq ($(PROGRAMMER),stk500v1)
AVRDUDE_FLAGS += -b $(AVRDUDE_BAUD)
endif
ifneq ($(filter flash flash-twi-hw-test verify verify-twi-hw-test read-signature read-fuses fuses read-hv-test read-freq-series arm-freq-series read-gm-test arm-gm-test,$(MAKECMDGOALS)),)
ifeq ($(PORT),)
$(error No Flipper ISP port. Start AVR Flasher USB ISP, or set PORT= and PROGRAMMER=)
endif
endif

ifdef PROTOCOL
BIN := bin/$(PROTOCOL)
else
BIN := bin/host
endif

# One-shot GM EEPROM test stays in the tree and is not linked.
SRC := src/main.c src/gpio.c src/gm_counter.c src/adc.c src/hv.c \
	src/twi_slave.c src/radsens.c src/radiation.c src/gn_config.c
OBJ := $(patsubst src/%.c,$(BIN)/%.o,$(SRC))

ELF := $(BIN)/$(TARGET).elf
HEX := $(BIN)/$(TARGET).hex
LST := $(BIN)/$(TARGET).lst

.PHONY: all clean size flash verify read-signature read-fuses toolchain-info disasm fuses read-hv-test read-freq-series arm-freq-series read-gm-test arm-gm-test twi-hw-test flash-twi-hw-test verify-twi-hw-test math-test

all: $(HEX) size

HOSTCC ?= gcc
EXTRA_HOST_CFLAGS ?=
MATH_TEST := $(BIN)/radiation_math_test

math-test: $(MATH_TEST)
	$(MATH_TEST)

$(MATH_TEST): host/radiation_math_test.c src/radiation.c include/radiation.h | $(BIN)
	$(HOSTCC) -std=c11 -Wall -Wextra -Wshadow -Iinclude $(EXTRA_HOST_CFLAGS) -o $@ \
		host/radiation_math_test.c src/radiation.c

# Hardware TWI0 proof of concept (issue #3). Separate image: HV off, fixed
# registers at 0x66. Does not replace the production bit-banged slave.
TWI_HW_TEST_SRC := src/twi_hw_test.c
TWI_HW_TEST_OBJ := $(BIN)/twi_hw_test.o
TWI_HW_TEST_ELF := $(BIN)/twi-hw-test.elf
TWI_HW_TEST_HEX := $(BIN)/twi-hw-test.hex

twi-hw-test: $(TWI_HW_TEST_HEX)
	$(SIZE) --format=berkeley $(TWI_HW_TEST_ELF)

$(TWI_HW_TEST_OBJ): $(TWI_HW_TEST_SRC) include/board.h | $(BIN)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TWI_HW_TEST_ELF): $(TWI_HW_TEST_OBJ) | $(BIN)
	$(CC) $(LDFLAGS) -o $@ $(TWI_HW_TEST_OBJ)

$(TWI_HW_TEST_HEX): $(TWI_HW_TEST_ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Flash only. Does not read or write fuses. Chip-erases EEPROM (HFUSE 0xD9).
flash-twi-hw-test: $(TWI_HW_TEST_HEX)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$(TWI_HW_TEST_HEX):i

verify-twi-hw-test: $(TWI_HW_TEST_HEX)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:v:$(TWI_HW_TEST_HEX):i


$(BIN):
	mkdir -p $@

$(ELF): $(OBJ) | $(BIN)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(BIN)/%.o: src/%.c include/board.h include/gpio.h include/gm_counter.h include/gm_test.h include/adc.h include/twi_slave.h include/radsens.h include/radiation.h include/hv.h include/gn_protocol.h include/gn_config.h | $(BIN)
	$(CC) $(CFLAGS) -c -o $@ $<

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# This toolchain's avr-size accepts berkeley, sysv, and gnu. It has no avr format.
size: $(ELF)
	$(SIZE) --format=berkeley $<

# Flash only. This target must not read or write fuses.
flash: $(HEX)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$(HEX):i

verify: $(HEX)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:v:$(HEX):i

read-signature:
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U signature:r:-:h

read-fuses:
	$(AVRDUDE) $(AVRDUDE_FLAGS) \
		-U lfuse:r:-:h \
		-U hfuse:r:-:h \
		-U efuse:r:-:h

# avrdude 8.1 prints its version, then exits 1 when -c is omitted.
toolchain-info:
	@echo "CC=$(CC)"
	@$(CC) --version
	@echo "OBJCOPY=$(OBJCOPY)"
	@$(OBJCOPY) --version
	@echo "SIZE=$(SIZE)"
	@$(SIZE) --version
	@echo "AVRDUDE=$$(command -v $(AVRDUDE) 2>/dev/null || echo $(AVRDUDE))"
	@$(AVRDUDE) -v; status=$$?; if [ $$status -ne 0 ] && [ $$status -ne 1 ]; then exit $$status; fi

disasm: $(ELF)
	$(OBJDUMP) -d -S $< > $(LST)

# Approved 2026-10-01 for this board. Do not change these bytes without a new review.
# This target writes fuses only. It does not program flash.
LFUSE := 0xE2
HFUSE := 0xD9
EFUSE := 0xF5

fuses:
	$(AVRDUDE) $(AVRDUDE_FLAGS) \
		-U lfuse:w:$(LFUSE):m \
		-U hfuse:w:$(HFUSE):m \
		-U efuse:w:$(EFUSE):m

# Read the one closed-loop result record. This does not write flash or fuses.
read-hv-test:
	mkdir -p $(BIN)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:r:$(BIN)/hv-test.bin:r
	python3 bringup/read_hv_test.py $(BIN)/hv-test.bin

# Read the frequency series. EEPROM read only. A reset cannot re-arm a
# COMPLETE or ABORTED series. Reading while the series is RUNNING aborts it.
read-freq-series:
	mkdir -p $(BIN)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:r:$(BIN)/freq-series.bin:r
	python3 bringup/read_freq_series.py $(BIN)/freq-series.bin

# Explicit re-arm. Changes the EEPROM arm word and checksum only, or writes
# one new ARMED record when EEPROM has no valid series. Does not write fuses
# or flash. The programmer reset that follows starts the 5 s delay.
arm-freq-series:
	mkdir -p $(BIN)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:r:$(BIN)/freq-series.bin:r
	python3 bringup/arm_freq_series.py $(BIN)/freq-series.bin $(BIN)/arm-cmds.txt
	@if [ ! -s $(BIN)/arm-cmds.txt ]; then \
		echo "EEPROM was already ARMED. No EEPROM bytes were written."; \
		exit 0; \
	fi
	$(AVRDUDE) $(AVRDUDE_FLAGS) -t < $(BIN)/arm-cmds.txt

# Read the SI-29BG one-shot record. EEPROM read only. Does not arm, does not
# start HV, and does not write fuses. A read resets the MCU. If the test was
# RUNNING, that reset latches ABORTED.
read-gm-test:
	mkdir -p $(BIN)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:r:$(BIN)/gm-test.bin:r
	python3 bringup/read_gm_test.py $(BIN)/gm-test.bin

# Explicit re-arm. Writes the GM record's state and checksum, or one new
# ARMED record at EEPROM address 256 when that record is missing. Does not
# write fuses or flash. The programmer reset that follows starts the 5 s delay.
arm-gm-test:
	mkdir -p $(BIN)
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U eeprom:r:$(BIN)/gm-test.bin:r
	python3 bringup/arm_gm_test.py $(BIN)/gm-test.bin $(BIN)/arm-gm-cmds.txt
	@if [ ! -s $(BIN)/arm-gm-cmds.txt ]; then \
		echo "EEPROM was already ARMED. No EEPROM bytes were written."; \
		exit 0; \
	fi
	$(AVRDUDE) $(AVRDUDE_FLAGS) -t < $(BIN)/arm-gm-cmds.txt

clean:
	rm -rf bin
