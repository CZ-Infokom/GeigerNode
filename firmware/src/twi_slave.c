#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>
#include <util/twi.h>

#include "radiation.h"
#include "twi_slave.h"

#define TWI_CR_ACK ((1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))
#define TWI_CR_RECOVER \
	((1 << TWINT) | (1 << TWSTO) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))

static volatile uint8_t regs[TWI_REG_COUNT];
static uint8_t txn[TWI_REG_COUNT];
static volatile uint8_t addr_ptr;
static uint8_t send_ptr;
static uint8_t expect_pointer;
static uint8_t have_new_pointer;

static void twi_store_u24(volatile uint8_t *dest, uint32_t value)
{
	value &= 0x00FFFFFFUL;
	dest[0] = (uint8_t)(value >> 16);
	dest[1] = (uint8_t)(value >> 8);
	dest[2] = (uint8_t)value;
}

static void twi_fill_const(volatile uint8_t *dest)
{
	dest[TWI_REG_DEVICE_ID] = TWI_REG_DEVICE_ID_VALUE;
	dest[TWI_REG_FW_VERSION] = TWI_REG_FW_VERSION_VALUE;
	dest[TWI_REG_ADDRESS] = TWI_SLAVE_ADDRESS;
	dest[TWI_REG_SENSITIVITY] =
		(uint8_t)(RADIATION_SI29BG_SENSITIVITY_REG & 0xFFU);
	dest[TWI_REG_SENSITIVITY + 1U] =
		(uint8_t)(RADIATION_SI29BG_SENSITIVITY_REG >> 8);
}

static void twi_latch(void)
{
	uint8_t i;

	for (i = 0; i < TWI_REG_COUNT; i++) {
		txn[i] = regs[i];
	}
	/* Constants win even if the live bank was disturbed. */
	twi_fill_const(txn);
}

static void twi_accept_pointer(uint8_t value)
{
	if (value >= TWI_REG_COUNT) {
		value = 0;
	}
	addr_ptr = value;
	expect_pointer = 0;
	have_new_pointer = 1;
}

static void twi_send_byte(void)
{
	TWDR0 = (send_ptr < TWI_REG_COUNT) ? txn[send_ptr] : 0;
	if (send_ptr < (TWI_REG_COUNT - 1U)) {
		send_ptr++;
	}
}

ISR(TWI0_vect)
{
	switch (TWSR0 & 0xF8) {
	case TW_SR_SLA_ACK:
	case TW_SR_ARB_LOST_SLA_ACK:
		expect_pointer = 1;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_DATA_ACK:
	case TW_SR_DATA_NACK:
		if (expect_pointer) {
			twi_accept_pointer(TWDR0);
		}
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_STOP:
		/* STOP and repeated START share this code. Do not treat
		 * leftover TWDR0 as a pointer; that restamps 0x03/0x06
		 * and every later read returns the 0x00 dose high byte.
		 */
		expect_pointer = 0;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_SLA_ACK:
	case TW_ST_ARB_LOST_SLA_ACK:
		twi_latch();
		if (have_new_pointer) {
			send_ptr = addr_ptr;
			have_new_pointer = 0;
		} else {
			send_ptr = 0;
		}
		twi_send_byte();
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_DATA_ACK:
		twi_send_byte();
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_DATA_NACK:
	case TW_ST_LAST_DATA:
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_BUS_ERROR:
		TWCR0 = TWI_CR_RECOVER;
		break;

	default:
		TWCR0 = TWI_CR_ACK;
		break;
	}
}

void twi_slave_init(void)
{
	uint8_t i;

	for (i = 0; i < TWI_REG_COUNT; i++) {
		regs[i] = 0;
	}
	twi_fill_const(regs);
	regs[TWI_REG_LED] = 0;

	addr_ptr = 0;
	send_ptr = 0;
	expect_pointer = 0;
	have_new_pointer = 0;

	/* Slave does not generate the clock. External 4.7 kOhm pull-ups hold the bus. */
	TWSR0 = 0;
	TWBR0 = 0;
	TWAR0 = TWI_SLAVE_ADDRESS << 1;
	TWAMR0 = 0;
	TWCR0 = TWI_CR_ACK;
}

void twi_slave_publish(uint32_t dynamic_x10,
		       uint32_t static_x10,
		       uint16_t pulses,
		       uint8_t hv_on)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		twi_fill_const(regs);
		twi_store_u24(&regs[TWI_REG_DYNAMIC_RAD], dynamic_x10);
		twi_store_u24(&regs[TWI_REG_STATIC_RAD], static_x10);
		regs[TWI_REG_PULSE_COUNT] = (uint8_t)(pulses >> 8);
		regs[TWI_REG_PULSE_COUNT + 1U] = (uint8_t)pulses;
		regs[TWI_REG_HV_ENABLE] = hv_on ? 1U : 0U;
		regs[TWI_REG_LED] = hv_on ? 1U : 0U;
	}
}
