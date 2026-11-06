/*
 * Hardware TWI0 proof of concept (issue #3).
 *
 * Standalone image: PB1 / HV_SW forced low, no Timer1, no ADC, no GM
 * counter, no pin-change I2C, no EEPROM writes. PD7 blinks a short pulse
 * about once a second so this image is obvious on the board.
 *
 * Slave address 0x66. Fixed register array. Status codes are recorded in
 * a 32-deep RAM ring and mirrored at 0x1F..0x2F for Flipper reads.
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>
#include <util/twi.h>

#include "board.h"

#define TWI_SLAVE_ADDRESS 0x66U

#define REG_DEVICE_ID   0x00U
#define REG_FW_VERSION  0x01U
#define REG_STATIC_RAD  0x06U
#define REG_SENSITIVITY 0x12U
#define REG_MAP_END     0x14U

#define REG_TRACE_COUNT 0x1FU
#define REG_TRACE_BASE  0x20U
#define REG_TRACE_LEN   16U

#define TRACE_DEPTH     32U

#define TWI_CR_ACK \
	((1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))
#define TWI_CR_RECOVER \
	((1 << TWINT) | (1 << TWSTO) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))

static volatile uint8_t regs[256];
static volatile uint8_t addr_ptr;
static volatile uint8_t send_ptr;
static volatile uint8_t expect_pointer;
static volatile uint8_t have_new_pointer;

static volatile uint8_t trace_buf[TRACE_DEPTH];
static volatile uint8_t trace_head;
static volatile uint8_t trace_count;
static volatile uint8_t trace_pub[REG_TRACE_LEN];
static volatile uint8_t trace_pub_count;

static void hv_sw_force_low(void)
{
	/* First instructions: latch HV_SW low, then drive it as an output. */
	HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
	HV_SW_DDR |= _BV(HV_SW_DDR_BIT);
}

static void led_init(void)
{
	LED_DDR |= _BV(LED_DDR_BIT);
	LED_PORT &= ~_BV(LED_PORT_BIT);
}

static void led_set(uint8_t on)
{
	if (on) {
		LED_PORT |= _BV(LED_PORT_BIT);
	} else {
		LED_PORT &= ~_BV(LED_PORT_BIT);
	}
}

static void regs_init(void)
{
	uint16_t i;

	for (i = 0; i < 256U; i++) {
		regs[i] = 0x00U;
	}

	regs[REG_DEVICE_ID] = 0x7DU;
	regs[REG_FW_VERSION] = 0x03U;
	regs[REG_STATIC_RAD] = 0x00U;
	regs[REG_STATIC_RAD + 1U] = 0x00U;
	regs[REG_STATIC_RAD + 2U] = 0x3CU;
	regs[REG_SENSITIVITY] = 0x41U;
	regs[REG_SENSITIVITY + 1U] = 0x00U;

	/* Past the published map, sequential reads return 0xFF. */
	for (i = (uint16_t)(REG_MAP_END + 1U); i < 256U; i++) {
		regs[i] = 0xFFU;
	}

	/* Trace window is filled from main. Start empty. */
	regs[REG_TRACE_COUNT] = 0U;
	for (i = 0; i < REG_TRACE_LEN; i++) {
		regs[REG_TRACE_BASE + i] = 0U;
	}
}

static void trace_push(uint8_t status)
{
	trace_buf[trace_head] = status;
	trace_head = (uint8_t)((trace_head + 1U) % TRACE_DEPTH);
	if (trace_count < TRACE_DEPTH) {
		trace_count++;
	}
}

static void twi_accept_pointer(uint8_t value)
{
	addr_ptr = value;
	expect_pointer = 0;
	have_new_pointer = 1;
}

static void twi_send_byte(void)
{
	TWDR0 = regs[send_ptr];
	send_ptr++;
}

static void twi_init(void)
{
	addr_ptr = 0;
	send_ptr = 0;
	expect_pointer = 0;
	have_new_pointer = 0;
	trace_head = 0;
	trace_count = 0;
	trace_pub_count = 0;

	/* Clear PRTWI0 so the peripheral is clocked. */
	PRR0 &= (uint8_t)~_BV(PRTWI0);

	/* PC4/PC5 stay inputs; external 4.7 kOhm pull-ups hold the bus. */
	DDRC &= (uint8_t)~(_BV(DDC4) | _BV(DDC5));
	PORTC &= (uint8_t)~(_BV(PORTC4) | _BV(PORTC5));

	TWSR0 = 0;
	TWBR0 = 0;
	TWAR0 = (uint8_t)(TWI_SLAVE_ADDRESS << 1);
	TWAMR0 = 0;
	TWCR0 = TWI_CR_ACK;
}

ISR(TWI0_vect)
{
	uint8_t status = (uint8_t)(TWSR0 & 0xF8U);

	trace_push(status);

	switch (status) {
	case TW_SR_SLA_ACK: /* 0x60 */
	case TW_SR_ARB_LOST_SLA_ACK: /* 0x68 */
		expect_pointer = 1;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_DATA_ACK: /* 0x80 */
	case TW_SR_DATA_NACK: /* 0x88 */
		if (expect_pointer) {
			twi_accept_pointer(TWDR0);
		}
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_STOP: /* 0xA0: STOP or repeated START */
		/* Do not read TWDR0. Keep addr_ptr for a following SLA+R. */
		expect_pointer = 0;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_SLA_ACK: /* 0xA8 */
	case TW_ST_ARB_LOST_SLA_ACK: /* 0xB0 */
		if (have_new_pointer) {
			send_ptr = addr_ptr;
			have_new_pointer = 0;
		} else {
			send_ptr = 0;
		}
		twi_send_byte();
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_DATA_ACK: /* 0xB8 */
		twi_send_byte();
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_DATA_NACK: /* 0xC0 */
	case TW_ST_LAST_DATA: /* 0xC8 */
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_BUS_ERROR: /* 0x00 */
		TWCR0 = TWI_CR_RECOVER;
		break;

	default:
		/* Return to listening. Never TWSTO except bus error. */
		TWCR0 = TWI_CR_ACK;
		break;
	}
}

static void publish_trace(void)
{
	uint8_t count;
	uint8_t head;
	uint8_t i;
	uint8_t start;
	uint8_t n;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		count = trace_count;
		head = trace_head;
		n = count;
		if (n > REG_TRACE_LEN) {
			n = REG_TRACE_LEN;
		}
		start = (uint8_t)((head + TRACE_DEPTH - n) % TRACE_DEPTH);
		for (i = 0; i < n; i++) {
			trace_pub[i] = trace_buf[(start + i) % TRACE_DEPTH];
		}
		trace_pub_count = n;
	}

	regs[REG_TRACE_COUNT] = trace_pub_count;
	for (i = 0; i < REG_TRACE_LEN; i++) {
		regs[REG_TRACE_BASE + i] = (i < trace_pub_count) ? trace_pub[i] : 0U;
	}
}

static void delay_ms(uint16_t ms)
{
	/*
	 * Busy wait at F_CPU. Enough for a visible LED blink; this image
	 * has no Timer2 tick.
	 */
	while (ms--) {
		uint16_t i;
		for (i = 0; i < (F_CPU / 4000UL); i++) {
			__asm__ __volatile__("nop");
		}
	}
}

int main(void)
{
	hv_sw_force_low();
	led_init();
	regs_init();
	twi_init();
	sei();

	for (;;) {
		publish_trace();
		led_set(1);
		delay_ms(50);
		led_set(0);
		delay_ms(950);
	}
}
