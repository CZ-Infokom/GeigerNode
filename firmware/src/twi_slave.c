#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "board.h"
#include "radiation.h"
#include "twi_slave.h"

#define I2C_SDA_MASK _BV(I2C_SDA_BIT)
#define I2C_SCL_MASK _BV(I2C_SCL_BIT)

static volatile uint8_t regs[2][TWI_REG_COUNT];
static volatile uint8_t *volatile regs_pub __attribute__((used));
static volatile uint8_t addr_ptr __attribute__((used));
static volatile uint8_t have_new_pointer __attribute__((used));
static volatile uint8_t last_pins __attribute__((used));
static volatile uint8_t twi_listen_addr __attribute__((used));
static volatile uint16_t pulse_since_read __attribute__((used));
static volatile uint8_t pulse_latch_lo __attribute__((used));
static volatile uint8_t pulse_latch_valid __attribute__((used));
static volatile uint8_t twi_write_ptr __attribute__((used));
static volatile uint8_t twi_write_len __attribute__((used));
static volatile uint8_t twi_write_data[2] __attribute__((used));
static volatile uint8_t twi_cmd_ptr;
static volatile uint8_t twi_cmd_len;
static volatile uint8_t twi_cmd_data[2];
static volatile uint8_t twi_cmd_ready;

static void twi_store_u24(volatile uint8_t *dest, uint32_t value)
{
	value &= 0x00FFFFFFUL;
	dest[0] = (uint8_t)(value >> 16);
	dest[1] = (uint8_t)(value >> 8);
	dest[2] = (uint8_t)value;
}

static void twi_fill_bank(volatile uint8_t *dest)
{
	uint8_t i;

	for (i = 0; i < TWI_REG_COUNT; i++) {
		dest[i] = 0;
	}
	dest[TWI_REG_DEVICE_ID] = TWI_REG_DEVICE_ID_VALUE;
	dest[TWI_REG_FW_VERSION] = TWI_REG_FW_VERSION_VALUE;
	dest[TWI_REG_ADDRESS] = TWI_SLAVE_ADDRESS;
	dest[TWI_REG_HV_ENABLE] = 1U;
	dest[TWI_REG_SENSITIVITY] =
		(uint8_t)(RADIATION_SI29BG_SENSITIVITY_REG & 0xFFU);
	dest[TWI_REG_SENSITIVITY + 1U] =
		(uint8_t)(RADIATION_SI29BG_SENSITIVITY_REG >> 8);
	dest[TWI_REG_LED] = 1U;
}

/*
 * PCINT1 is naked. A C prologue pushes enough registers to miss Flipper's
 * first address bit: START hold is about 4 us and the following SCL low
 * is about 6 us. This handler saves four registers, waits for that low,
 * and clocks the address itself. SCL is never driven.
 *
 * r0, r24, and r25 are scratch once SREG is on the stack. A confirmed
 * START clears the write length and the pulse-low latch. The listen
 * address is loaded from RAM. Data bytes after the pointer are sampled
 * and committed when the handler exits. Registers 0x09 and 0x0A are
 * filled from the live pulse counter, which that read clears.
 */
ISR(PCINT1_vect, ISR_NAKED)
{
	asm volatile(
		"push r0\n\t"
		"in r0, %[sreg]\n\t"
		"push r0\n\t"
		"push r24\n\t"
		"push r25\n\t"
		"in r24, %[pin]\n\t"
		"lds r25, last_pins\n\t"
		"sts last_pins, r24\n\t"
		"sbrs r24, %[sclbit]\n\t"
		"rjmp twi_isr_done\n\t"
		"sbrs r25, %[sdabit]\n\t"
		"rjmp twi_isr_done\n\t"
		"sbrc r24, %[sdabit]\n\t"
		"rjmp twi_isr_done\n\t"
		"clr r0\n\t"
		"sts twi_write_len, r0\n\t"
		"sts pulse_latch_valid, r0\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"rcall twi_read8\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"mov r24, r25\n\t"
		"lsr r24\n\t"
		"lds r0, twi_listen_addr\n\t"
		"cp r24, r0\n\t"
		"breq 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"sbi %[ddr], %[sdabit]\n\t"
		/* ACK is sampled on the next high. Release only after it falls. */
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		/* Load the read byte during this high, then drive bit 7 on the fall. */
		"sbrc r25, 0\n\t"
		"rjmp twi_read_data\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"rcall twi_read8\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"sbi %[ddr], %[sdabit]\n\t"
		"cpi r25, %[nreg]\n\t"
		"brlo twi_ptr_ok\n\t"
		"ldi r25, 0\n\t"
		"twi_ptr_ok:\n\t"
		"sts addr_ptr, r25\n\t"
		"sts twi_write_ptr, r25\n\t"
		"ldi r24, 1\n\t"
		"sts have_new_pointer, r24\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"cbi %[port], %[sdabit]\n\t"
		/* Pointer byte is done. Next is STOP, another byte, or Sr. */
		"twi_after_ptr:\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"in r0, %[pin]\n\t"
		"ldi r24, 0\n\t"
		"twi_watch_sr:\n\t"
		"dec r24\n\t"
		"brne 1f\n\t"
		"rjmp twi_isr_done\n\t"
		"1:\n\t"
		"sbis %[pin], %[sclbit]\n\t"
		"rjmp twi_extra_byte\n\t"
		"in r25, %[pin]\n\t"
		"sbrs r0, %[sdabit]\n\t"
		"rjmp twi_watch_upd\n\t"
		"sbrc r25, %[sdabit]\n\t"
		"rjmp twi_watch_upd\n\t"
		/* Previous SDA was high and it is low now: repeated START. */
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"rcall twi_read8\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"mov r24, r25\n\t"
		"lsr r24\n\t"
		"lds r0, twi_listen_addr\n\t"
		"cp r24, r0\n\t"
		"breq 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"sbi %[ddr], %[sdabit]\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"sbrs r25, 0\n\t"
		"rjmp twi_write_again\n\t"
		"rjmp twi_read_data\n\t"
		"twi_write_again:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"rjmp twi_after_ptr\n\t"
		"twi_read_data:\n\t"
		"push r22\n\t"
		"push r23\n\t"
		"push r26\n\t"
		"push r27\n\t"
		"ldi r22, %[nreg]\n\t"
		"lds r24, have_new_pointer\n\t"
		"tst r24\n\t"
		"breq twi_ptr_zero\n\t"
		"lds r24, addr_ptr\n\t"
		"rjmp twi_ptr_have\n\t"
		"twi_ptr_zero:\n\t"
		"ldi r24, 0\n\t"
		"twi_ptr_have:\n\t"
		"ldi r25, 0\n\t"
		"sts have_new_pointer, r25\n\t"
		"sts addr_ptr, r24\n\t"
		"rcall twi_fetch_byte\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_read_leave\n\t1:\n\t"
		"rjmp twi_send_byte\n\t"
		"twi_next_byte:\n\t"
		"lds r24, addr_ptr\n\t"
		"cpi r24, %[last]\n\t"
		"brsh twi_send_ff\n\t"
		"inc r24\n\t"
		"twi_load_send:\n\t"
		"sts addr_ptr, r24\n\t"
		"rcall twi_fetch_byte\n\t"
		"rjmp twi_send_byte\n\t"
		"twi_send_ff:\n\t"
		"ldi r25, 0xFF\n\t"
		"twi_send_byte:\n\t"
		"ldi r23, 8\n\t"
		"twi_send_bit:\n\t"
		"lsl r25\n\t"
		"brcs twi_bit_one\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"sbi %[ddr], %[sdabit]\n\t"
		"rjmp twi_bit_clock\n\t"
		"twi_bit_one:\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"twi_bit_clock:\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_read_leave\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_read_leave\n\t1:\n\t"
		"dec r23\n\t"
		"brne twi_send_bit\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_read_leave\n\t1:\n\t"
		"in r0, %[pin]\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_read_leave\n\t1:\n\t"
		"sbrc r0, %[sdabit]\n\t"
		"rjmp twi_read_leave\n\t"
		"dec r22\n\t"
		"brne twi_next_byte\n\t"
		"twi_read_leave:\n\t"
		"pop r27\n\t"
		"pop r26\n\t"
		"pop r23\n\t"
		"pop r22\n\t"
		"rjmp twi_rel_done\n\t"
		"twi_watch_upd:\n\t"
		"mov r0, r25\n\t"
		"rjmp twi_watch_sr\n\t"
		"twi_extra_byte:\n\t"
		"rcall twi_read8\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_isr_done\n\t1:\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"sbi %[ddr], %[sdabit]\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_rel_done\n\t1:\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"lds r24, twi_write_len\n\t"
		"cpi r24, 2\n\t"
		"brsh twi_extra_stored\n\t"
		"cpi r24, 0\n\t"
		"breq twi_extra_store0\n\t"
		"sts twi_write_data+1, r25\n\t"
		"ldi r24, 2\n\t"
		"rjmp twi_extra_have\n\t"
		"twi_extra_store0:\n\t"
		"sts twi_write_data, r25\n\t"
		"ldi r24, 1\n\t"
		"twi_extra_have:\n\t"
		"sts twi_write_len, r24\n\t"
		"twi_extra_stored:\n\t"
		"rjmp twi_after_ptr\n\t"
		"twi_rel_done:\n\t"
		"cbi %[ddr], %[sdabit]\n\t"
		"cbi %[port], %[sdabit]\n\t"
		"twi_isr_done:\n\t"
		"lds r24, twi_write_len\n\t"
		"tst r24\n\t"
		"breq twi_isr_restore\n\t"
		"sts twi_cmd_len, r24\n\t"
		"lds r25, twi_write_ptr\n\t"
		"sts twi_cmd_ptr, r25\n\t"
		"lds r25, twi_write_data\n\t"
		"sts twi_cmd_data, r25\n\t"
		"lds r25, twi_write_data+1\n\t"
		"sts twi_cmd_data+1, r25\n\t"
		"ldi r25, 1\n\t"
		"sts twi_cmd_ready, r25\n\t"
		"clr r24\n\t"
		"sts twi_write_len, r24\n\t"
		"twi_isr_restore:\n\t"
		"in r24, %[pin]\n\t"
		"sts last_pins, r24\n\t"
		"ldi r24, %[pcif1]\n\t"
		"out %[pcifr], r24\n\t"
		"pop r25\n\t"
		"pop r24\n\t"
		"pop r0\n\t"
		"out %[sreg], r0\n\t"
		"pop r0\n\t"
		"reti\n\t"
		/*
		 * Address in r24. Byte comes back in r25. r26 and r27 are
		 * scratch and are already saved on the read path. 0x09 clears
		 * the live counter and latches its low byte for a following
		 * 0x0A in this burst.
		 */
		"twi_fetch_byte:\n\t"
		"cpi r24, %[pulsehi]\n\t"
		"breq twi_fetch_hi\n\t"
		"cpi r24, %[pulselo]\n\t"
		"breq twi_fetch_lo\n\t"
		"lds r26, regs_pub\n\t"
		"lds r27, regs_pub+1\n\t"
		"add r26, r24\n\t"
		"clr r0\n\t"
		"adc r27, r0\n\t"
		"ld r25, X\n\t"
		"ret\n\t"
		"twi_fetch_hi:\n\t"
		"lds r25, pulse_since_read\n\t"
		"lds r0, pulse_since_read+1\n\t"
		"sts pulse_latch_lo, r25\n\t"
		"mov r25, r0\n\t"
		"clr r0\n\t"
		"sts pulse_since_read, r0\n\t"
		"sts pulse_since_read+1, r0\n\t"
		"ldi r24, 1\n\t"
		"sts pulse_latch_valid, r24\n\t"
		"ret\n\t"
		"twi_fetch_lo:\n\t"
		"lds r0, pulse_latch_valid\n\t"
		"tst r0\n\t"
		"breq twi_fetch_lo_fresh\n\t"
		"lds r25, pulse_latch_lo\n\t"
		"clr r0\n\t"
		"sts pulse_latch_valid, r0\n\t"
		"ret\n\t"
		"twi_fetch_lo_fresh:\n\t"
		"lds r25, pulse_since_read\n\t"
		"clr r0\n\t"
		"sts pulse_since_read, r0\n\t"
		"sts pulse_since_read+1, r0\n\t"
		"sts pulse_latch_valid, r0\n\t"
		"ret\n\t"
		/* SCL low already. Read 8 bits. Return the byte in r25, C set. */
		"twi_read8:\n\t"
		"push r22\n\t"
		"ldi r25, 0\n\t"
		"ldi r22, 8\n\t"
		"twi_read8_bit:\n\t"
		"rcall twi_wait_hi\n\t"
		"brcs 1f\n\trjmp twi_read8_fail\n\t1:\n\t"
		"lsl r25\n\t"
		"sbic %[pin], %[sdabit]\n\t"
		"ori r25, 1\n\t"
		"dec r22\n\t"
		"breq twi_read8_ok\n\t"
		"rcall twi_wait_lo\n\t"
		"brcs 1f\n\trjmp twi_read8_fail\n\t1:\n\t"
		"rjmp twi_read8_bit\n\t"
		"twi_read8_ok:\n\t"
		"pop r22\n\t"
		"sec\n\t"
		"ret\n\t"
		"twi_read8_fail:\n\t"
		"pop r22\n\t"
		"clc\n\t"
		"ret\n\t"
		"twi_wait_hi:\n\t"
		"ldi r24, 0\n\t"
		"twi_wait_hi_loop:\n\t"
		"dec r24\n\t"
		"breq twi_wait_fail\n\t"
		"sbis %[pin], %[sclbit]\n\t"
		"rjmp twi_wait_hi_loop\n\t"
		"sec\n\t"
		"ret\n\t"
		"twi_wait_lo:\n\t"
		"ldi r24, 0\n\t"
		"twi_wait_lo_loop:\n\t"
		"dec r24\n\t"
		"breq twi_wait_fail\n\t"
		"sbic %[pin], %[sclbit]\n\t"
		"rjmp twi_wait_lo_loop\n\t"
		"sec\n\t"
		"ret\n\t"
		"twi_wait_fail:\n\t"
		"clc\n\t"
		"ret\n\t"
		:
		:
		[sreg] "I"(_SFR_IO_ADDR(SREG)),
		[pin] "I"(_SFR_IO_ADDR(PINC)),
		[ddr] "I"(_SFR_IO_ADDR(DDRC)),
		[port] "I"(_SFR_IO_ADDR(PORTC)),
		[pcifr] "I"(_SFR_IO_ADDR(PCIFR)),
		[sclbit] "I"(I2C_SCL_BIT),
		[sdabit] "I"(I2C_SDA_BIT),
		[nreg] "M"(TWI_REG_COUNT),
		[pulsehi] "M"(TWI_REG_PULSE_COUNT),
		[pulselo] "M"(TWI_REG_PULSE_COUNT + 1U),
		[last] "M"(TWI_REG_COUNT - 1),
		[pcif1] "M"(1 << PCIF1)
		:
		"memory");
}

static void twi_put(uint8_t reg, uint8_t value)
{
	regs[0][reg] = value;
	regs[1][reg] = value;
}

void twi_slave_init(void)
{
	twi_fill_bank(regs[0]);
	twi_fill_bank(regs[1]);
	regs_pub = regs[0];
	addr_ptr = 0;
	have_new_pointer = 0;
	twi_listen_addr = TWI_SLAVE_ADDRESS;
	pulse_since_read = 0;
	pulse_latch_lo = 0;
	pulse_latch_valid = 0;
	twi_write_ptr = 0;
	twi_write_len = 0;
	twi_write_data[0] = 0;
	twi_write_data[1] = 0;
	twi_cmd_ptr = 0;
	twi_cmd_len = 0;
	twi_cmd_data[0] = 0;
	twi_cmd_data[1] = 0;
	twi_cmd_ready = 0;

	TWCR0 = 0;
	DIDR0 &= (uint8_t)~(_BV(ADC4D) | _BV(ADC5D));
	I2C_SDA_DDR &= ~I2C_SDA_MASK;
	I2C_SCL_DDR &= ~I2C_SCL_MASK;
	I2C_SDA_PORT &= ~I2C_SDA_MASK;
	I2C_SCL_PORT &= ~I2C_SCL_MASK;

	last_pins = I2C_SCL_PIN;
	PCMSK1 |= I2C_SDA_MASK;
	PCMSK1 &= (uint8_t)~I2C_SCL_MASK;
	PCIFR = _BV(PCIF1);
	PCICR |= _BV(PCIE1);
}

void twi_slave_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10)
{
	uint8_t bank;
	volatile uint8_t *dest;
	uint8_t i;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		bank = (uint8_t)((regs_pub == regs[1]) ? 0U : 1U);
		dest = regs[bank];
		for (i = 0; i < TWI_REG_COUNT; i++) {
			dest[i] = regs_pub[i];
		}
		twi_store_u24(&dest[TWI_REG_DYNAMIC_RAD], dynamic_x10);
		twi_store_u24(&dest[TWI_REG_STATIC_RAD], static_x10);
		regs_pub = dest;
	}
}

void twi_slave_count_pulses(uint16_t count)
{
	uint32_t sum;

	if (count == 0U) {
		return;
	}
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		sum = (uint32_t)pulse_since_read + count;
		if (sum > 65535UL) {
			pulse_since_read = 65535U;
		} else {
			pulse_since_read = (uint16_t)sum;
		}
	}
}

void twi_slave_clear_pulses(void)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		pulse_since_read = 0;
	}
}

void twi_slave_set_address(uint8_t address)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		twi_listen_addr = address;
		twi_put(TWI_REG_ADDRESS, address);
	}
}

void twi_slave_set_sensitivity(uint16_t sensitivity)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		twi_put(TWI_REG_SENSITIVITY, (uint8_t)sensitivity);
		twi_put((uint8_t)(TWI_REG_SENSITIVITY + 1U),
			(uint8_t)(sensitivity >> 8));
	}
}

void twi_slave_set_hv_command(uint8_t on)
{
	twi_put(TWI_REG_HV_ENABLE, on ? 1U : 0U);
}

void twi_slave_set_led(uint8_t on)
{
	twi_put(TWI_REG_LED, on ? 1U : 0U);
}

void twi_slave_set_lp(uint8_t on)
{
	twi_put(TWI_REG_LP_MODE, on ? 1U : 0U);
}

uint8_t twi_slave_take_write(struct twi_write_cmd *out)
{
	uint8_t ready;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		ready = twi_cmd_ready;
		if (ready) {
			out->ptr = twi_cmd_ptr;
			out->len = twi_cmd_len;
			out->data[0] = twi_cmd_data[0];
			out->data[1] = twi_cmd_data[1];
			twi_cmd_ready = 0;
		}
	}
	return ready;
}
