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

SRC := src/main.c
OBJ := $(patsubst src/%.c,$(BIN)/%.o,$(SRC))

ELF := $(BIN)/$(TARGET).elf
HEX := $(BIN)/$(TARGET).hex
LST := $(BIN)/$(TARGET).lst

.PHONY: all clean size flash verify read-signature read-fuses toolchain-info disasm fuses

all: $(HEX) size

$(BIN):
	mkdir -p $@

$(ELF): $(OBJ) | $(BIN)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(BIN)/%.o: src/%.c include/board.h | $(BIN)
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

clean:
	rm -rf $(BIN)
