#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/twi.h>

#include "twi_slave.h"

#define TWI_REG_COUNT 16

#define TWI_CR_ACK ((1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))

static volatile uint8_t regs[TWI_REG_COUNT];
static volatile uint8_t reg_ptr;
static uint8_t next_byte_is_pointer;

static uint8_t reg_at(uint8_t index)
{
	if (index < TWI_REG_COUNT) {
		return regs[index];
	}
	return 0;
}

static void twi_load_next(void)
{
	TWDR0 = reg_at(reg_ptr);
	if (reg_ptr < (TWI_REG_COUNT - 1)) {
		reg_ptr++;
	}
}

ISR(TWI0_vect)
{
	switch (TWSR0 & 0xF8) {
	case TW_SR_SLA_ACK:
	case TW_SR_ARB_LOST_SLA_ACK:
		next_byte_is_pointer = 1;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_DATA_ACK:
		if (next_byte_is_pointer) {
			reg_ptr = TWDR0;
			next_byte_is_pointer = 0;
		}
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_STOP:
		/* Repeated START arrives here too. Keep reg_ptr. */
		next_byte_is_pointer = 0;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_SLA_ACK:
	case TW_ST_ARB_LOST_SLA_ACK:
	case TW_ST_DATA_ACK:
		twi_load_next();
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_DATA_NACK:
	case TW_ST_LAST_DATA:
		TWCR0 = TWI_CR_ACK;
		break;

	default:
		TWCR0 = (1 << TWINT) | (1 << TWSTO) | (1 << TWEA) | (1 << TWEN) |
			(1 << TWIE);
		break;
	}
}

void twi_slave_init(void)
{
	uint8_t i;

	for (i = 0; i < TWI_REG_COUNT; i++) {
		regs[i] = 0;
	}
	regs[TWI_REG_DEVICE_ID] = TWI_REG_DEVICE_ID_VALUE;
	regs[TWI_REG_STATIC_RAD] = (TWI_STATIC_RAD_VALUE >> 16) & 0xFF;
	regs[TWI_REG_STATIC_RAD + 1] = (TWI_STATIC_RAD_VALUE >> 8) & 0xFF;
	regs[TWI_REG_STATIC_RAD + 2] = TWI_STATIC_RAD_VALUE & 0xFF;

	reg_ptr = 0;
	next_byte_is_pointer = 0;

	/* Slave does not generate the clock. External 4.7 kOhm pull-ups hold the bus. */
	TWSR0 = 0;
	TWBR0 = 0;
	TWAR0 = TWI_SLAVE_ADDRESS << 1;
	TWAMR0 = 0;
	TWCR0 = TWI_CR_ACK;
}
