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

TOOLCHAIN := /opt/avr8-gnu-toolchain-4.0.0

TARGET := geigernode
MCU := atmega328pb
AVRDUDE_MCU := m328pb

CC := $(TOOLCHAIN)/bin/avr-gcc
OBJCOPY := $(TOOLCHAIN)/bin/avr-objcopy
OBJDUMP := $(TOOLCHAIN)/bin/avr-objdump
SIZE := $(TOOLCHAIN)/bin/avr-size
AVRDUDE := avrdude

PROGRAMMER := atmelice_isp
PORT := usb

F_CPU ?= 8000000UL

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

LDFLAGS := -mmcu=$(MCU) -Wl,--gc-sections

AVRDUDE_FLAGS := -p $(AVRDUDE_MCU) -c $(PROGRAMMER) -P $(PORT)

BIN := bin

# This image does not link TWI or the radiation registers. Those sources
# stay in the tree for the later host test. Linking them would install the
# TWI vector during the HV run.
SRC := src/main.c src/gpio.c src/gm_counter.c src/gm_test.c src/adc.c src/hv.c
OBJ := $(patsubst src/%.c,$(BIN)/%.o,$(SRC))

ELF := $(BIN)/$(TARGET).elf
HEX := $(BIN)/$(TARGET).hex
LST := $(BIN)/$(TARGET).lst

.PHONY: all clean size flash verify read-signature read-fuses toolchain-info disasm fuses read-hv-test read-freq-series arm-freq-series read-gm-test arm-gm-test

all: $(HEX) size

$(BIN):
	mkdir -p $@

$(ELF): $(OBJ) | $(BIN)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(BIN)/%.o: src/%.c include/board.h include/gpio.h include/gm_counter.h include/gm_test.h include/adc.h include/twi_slave.h include/radsens.h include/hv.h | $(BIN)
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
	rm -rf $(BIN)
