#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>
#include <util/twi.h>

#include "board.h"
#include "gm_counter.h"
#include "radiation.h"
#include "twi_slave.h"

#define TWI_CR_ACK \
	((1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))
#define TWI_CR_RECOVER \
	((1 << TWINT) | (1 << TWSTO) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE))

static volatile uint8_t regs[2][TWI_REG_COUNT];
static volatile uint8_t *volatile regs_pub;
static volatile uint8_t *volatile read_bank;
static volatile uint8_t addr_ptr;
static uint8_t send_ptr;
static uint8_t expect_pointer;
static uint8_t have_new_pointer;
#ifndef GEIGERNODE_PROTOCOL_NATIVE
static volatile uint16_t pulse_since_read;
static uint8_t pulse_latch_lo;
static uint8_t pulse_latch_valid;
#endif
static uint8_t twi_write_ptr;
static uint8_t twi_write_len;
static uint8_t twi_write_data[TWI_WRITE_MAX];
static volatile uint8_t twi_cmd_ptr;
static volatile uint8_t twi_cmd_len;
static volatile uint8_t twi_cmd_data[TWI_WRITE_MAX];
static volatile uint8_t twi_cmd_ready;

#ifdef GEIGERNODE_PROTOCOL_NATIVE

static void twi_store_u16(volatile uint8_t *dest, uint16_t value)
{
	dest[0] = (uint8_t)(value >> 8);
	dest[1] = (uint8_t)value;
}

static void twi_store_u32(volatile uint8_t *dest, uint32_t value)
{
	dest[0] = (uint8_t)(value >> 24);
	dest[1] = (uint8_t)(value >> 16);
	dest[2] = (uint8_t)(value >> 8);
	dest[3] = (uint8_t)value;
}

static void twi_fill_bank(volatile uint8_t *dest)
{
	uint8_t i;

	for (i = 0; i < TWI_REG_COUNT; i++) {
		dest[i] = 0;
	}
	twi_store_u16(&dest[GN_REG_DEVICE_ID], GN_DEVICE_ID);
	dest[GN_REG_PROTOCOL] = GN_PROTOCOL_VERSION;
	dest[GN_REG_CAPS] = GN_CAPS_VALUE;
	twi_store_u32(&dest[GN_REG_CAL], RADIATION_SBM20_NSVH_PER_CPM_Q16);
	twi_store_u16(&dest[GN_REG_WINDOW_S], GN_WINDOW_DEFAULT_S);
	twi_store_u16(&dest[GN_REG_HOLDOFF_US], GN_HOLDOFF_DEFAULT_US);
	twi_store_u32(&dest[GN_REG_MIN_INTERVAL], 0xFFFFFFFFUL);
}

#else

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
	uint16_t sens = gm_tube_sensitivity_reg();

	for (i = 0; i < TWI_REG_COUNT; i++) {
		dest[i] = 0;
	}
	dest[TWI_REG_DEVICE_ID] = TWI_REG_DEVICE_ID_VALUE;
	dest[TWI_REG_FW_VERSION] = TWI_REG_FW_VERSION_VALUE;
	dest[TWI_REG_ADDRESS] = TWI_SLAVE_ADDRESS;
	dest[TWI_REG_HV_ENABLE] = 1U;
	dest[TWI_REG_SENSITIVITY] = (uint8_t)sens;
	dest[TWI_REG_SENSITIVITY + 1U] = (uint8_t)(sens >> 8);
	dest[TWI_REG_LED] = 1U;
}

#endif

/*
 * Called only inside ATOMIC_BLOCK. Returns a bank that is not frozen
 * by an in-flight read. When the published bank is frozen, the idle
 * bank is filled from it so the reader keeps the old snapshot.
 */
static volatile uint8_t *twi_begin_update(void)
{
	volatile uint8_t *dest;
	volatile uint8_t *other;
	uint8_t i;

	other = (regs_pub == regs[0]) ? regs[1] : regs[0];
	if (regs_pub == read_bank) {
		dest = other;
		for (i = 0; i < TWI_REG_COUNT; i++) {
			dest[i] = regs_pub[i];
		}
	} else if (other == read_bank) {
		dest = regs_pub;
	} else {
		dest = other;
		for (i = 0; i < TWI_REG_COUNT; i++) {
			dest[i] = regs_pub[i];
		}
	}
	return dest;
}

#ifndef GEIGERNODE_PROTOCOL_NATIVE
static void twi_put(uint8_t reg, uint8_t value)
{
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
		dest[reg] = value;
		regs_pub = dest;
	}
}
#endif

static void twi_accept_pointer(uint8_t value)
{
	if (value >= TWI_REG_COUNT) {
		value = 0;
	}
	addr_ptr = value;
	twi_write_ptr = value;
	expect_pointer = 0;
	have_new_pointer = 1;
	twi_write_len = 0;
}

#ifndef GEIGERNODE_PROTOCOL_NATIVE

static uint8_t twi_pulse_hi(void)
{
	uint16_t count = pulse_since_read;

	pulse_latch_lo = (uint8_t)count;
	pulse_since_read = 0;
	pulse_latch_valid = 1;
	return (uint8_t)(count >> 8);
}

static uint8_t twi_pulse_lo(void)
{
	uint8_t value;

	if (pulse_latch_valid) {
		value = pulse_latch_lo;
		pulse_latch_valid = 0;
		return value;
	}
	value = (uint8_t)pulse_since_read;
	pulse_since_read = 0;
	return value;
}

#endif

static void twi_send_byte(void)
{
	uint8_t byte;
	volatile uint8_t *bank = read_bank;

	if (send_ptr >= TWI_REG_COUNT || bank == 0) {
		byte = 0xFFU;
#ifndef GEIGERNODE_PROTOCOL_NATIVE
	} else if (send_ptr == TWI_REG_PULSE_COUNT) {
		byte = twi_pulse_hi();
	} else if (send_ptr == (uint8_t)(TWI_REG_PULSE_COUNT + 1U)) {
		byte = twi_pulse_lo();
#endif
	} else {
		byte = bank[send_ptr];
	}
	TWDR0 = byte;
	if (send_ptr < 255U) {
		send_ptr++;
	}
}

static void twi_commit_write(void)
{
	uint8_t i;

	if (twi_write_len == 0U) {
		return;
	}
	twi_cmd_ptr = twi_write_ptr;
	twi_cmd_len = twi_write_len;
	for (i = 0; i < twi_write_len; i++) {
		twi_cmd_data[i] = twi_write_data[i];
	}
	twi_cmd_ready = 1;
	twi_write_len = 0;
}

ISR(TWI0_vect)
{
	uint8_t status = (uint8_t)(TWSR0 & 0xF8U);
	uint8_t data;

	switch (status) {
	case TW_SR_SLA_ACK: /* 0x60 */
	case TW_SR_ARB_LOST_SLA_ACK: /* 0x68 */
		expect_pointer = 1;
		twi_write_len = 0;
#ifndef GEIGERNODE_PROTOCOL_NATIVE
		pulse_latch_valid = 0;
#endif
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_DATA_ACK: /* 0x80 */
	case TW_SR_DATA_NACK: /* 0x88 */
		data = TWDR0;
		if (expect_pointer) {
			twi_accept_pointer(data);
		} else if (twi_write_len < TWI_WRITE_MAX) {
			twi_write_data[twi_write_len] = data;
			twi_write_len++;
		}
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_SR_STOP: /* 0xA0: STOP or repeated START */
		/* Do not read TWDR0. Keep addr_ptr for a following SLA+R. */
		expect_pointer = 0;
		twi_commit_write();
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_ST_SLA_ACK: /* 0xA8 */
	case TW_ST_ARB_LOST_SLA_ACK: /* 0xB0 */
		read_bank = regs_pub;
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
		read_bank = 0;
		TWCR0 = TWI_CR_ACK;
		break;

	case TW_BUS_ERROR: /* 0x00 */
		expect_pointer = 0;
		twi_write_len = 0;
		read_bank = 0;
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

	twi_fill_bank(regs[0]);
	twi_fill_bank(regs[1]);
	regs_pub = regs[0];
	read_bank = 0;
	addr_ptr = 0;
	send_ptr = 0;
	expect_pointer = 0;
	have_new_pointer = 0;
#ifndef GEIGERNODE_PROTOCOL_NATIVE
	pulse_since_read = 0;
	pulse_latch_lo = 0;
	pulse_latch_valid = 0;
#endif
	twi_write_ptr = 0;
	twi_write_len = 0;
	twi_cmd_ptr = 0;
	twi_cmd_len = 0;
	twi_cmd_ready = 0;
	for (i = 0; i < TWI_WRITE_MAX; i++) {
		twi_write_data[i] = 0;
		twi_cmd_data[i] = 0;
	}

	/* Pin-change bit-bang stays off. Hardware TWI0 owns PC4/PC5. */
	PCICR &= (uint8_t)~_BV(PCIE1);
	PCMSK1 &= (uint8_t)~(_BV(PCINT12) | _BV(PCINT13));
	TWCR0 = 0;

	PRR0 &= (uint8_t)~_BV(PRTWI0);
	DIDR0 &= (uint8_t)~(_BV(ADC4D) | _BV(ADC5D));
	I2C_SDA_DDR &= ~_BV(I2C_SDA_BIT);
	I2C_SCL_DDR &= ~_BV(I2C_SCL_BIT);
	I2C_SDA_PORT &= ~_BV(I2C_SDA_BIT);
	I2C_SCL_PORT &= ~_BV(I2C_SCL_BIT);

	TWSR0 = 0;
	TWBR0 = 0;
	TWAR0 = (uint8_t)(TWI_SLAVE_ADDRESS << 1);
	TWAMR0 = 0;
	TWCR0 = TWI_CR_ACK;
}

#ifdef GEIGERNODE_PROTOCOL_NATIVE

void twi_slave_publish_native(uint32_t dose_nsvh, uint32_t cpm_x10,
			      uint16_t status, uint32_t sequence,
			      uint32_t cal_q16, uint32_t count,
			      uint32_t elapsed_ms,
			      const struct gm_histogram *histogram)
{
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
		twi_store_u16(&dest[GN_REG_DEVICE_ID], GN_DEVICE_ID);
		dest[GN_REG_PROTOCOL] = GN_PROTOCOL_VERSION;
		dest[GN_REG_CAPS] = GN_CAPS_VALUE;
		twi_store_u32(&dest[GN_REG_DOSE], dose_nsvh);
		twi_store_u32(&dest[GN_REG_CPM_X10], cpm_x10);
		twi_store_u16(&dest[GN_REG_STATUS], status);
		twi_store_u32(&dest[GN_REG_SEQUENCE], sequence);
		twi_store_u32(&dest[GN_REG_CAL], cal_q16);
		twi_store_u32(&dest[GN_REG_COUNT], count);
		twi_store_u32(&dest[GN_REG_ELAPSED], elapsed_ms);
		twi_store_u32(&dest[GN_REG_MIN_INTERVAL],
			      histogram->min_interval_us);
		twi_store_u16(&dest[GN_REG_BIN_LT_50US], histogram->bins[0]);
		twi_store_u16(&dest[GN_REG_BIN_50_100US], histogram->bins[1]);
		twi_store_u16(&dest[GN_REG_BIN_100_200US], histogram->bins[2]);
		twi_store_u16(&dest[GN_REG_BIN_200_500US], histogram->bins[3]);
		twi_store_u16(&dest[GN_REG_BIN_500US_1MS], histogram->bins[4]);
		twi_store_u16(&dest[GN_REG_BIN_1_2MS], histogram->bins[5]);
		twi_store_u16(&dest[GN_REG_BIN_2_5MS], histogram->bins[6]);
		twi_store_u16(&dest[GN_REG_BIN_5_10MS], histogram->bins[7]);
		twi_store_u16(&dest[GN_REG_BIN_GE_10MS], histogram->bins[8]);
		dest[GN_REG_HISTOGRAM_FLAGS] = histogram->flags;
		regs_pub = dest;
	}
}

void twi_slave_set_native_status(uint16_t status)
{
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
		twi_store_u16(&dest[GN_REG_STATUS], status);
		regs_pub = dest;
	}
}

void twi_slave_set_native_config(uint16_t window_s, uint16_t holdoff_us,
				 uint32_t cal_q16)
{
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
		twi_store_u16(&dest[GN_REG_WINDOW_S], window_s);
		twi_store_u16(&dest[GN_REG_HOLDOFF_US], holdoff_us);
		twi_store_u32(&dest[GN_REG_CAL], cal_q16);
		regs_pub = dest;
	}
}

void twi_slave_clear_native_histogram(void)
{
	volatile uint8_t *dest;
	uint8_t i;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
		twi_store_u32(&dest[GN_REG_MIN_INTERVAL], 0xFFFFFFFFUL);
		for (i = GN_REG_BIN_LT_50US; i <= GN_REG_HISTOGRAM_FLAGS; i++) {
			dest[i] = 0;
		}
		regs_pub = dest;
	}
}

#else

void twi_slave_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10)
{
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
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
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		TWAR0 = (uint8_t)(address << 1);
		dest = twi_begin_update();
		dest[TWI_REG_ADDRESS] = address;
		regs_pub = dest;
	}
}

void twi_slave_set_sensitivity(uint16_t sensitivity)
{
	volatile uint8_t *dest;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		dest = twi_begin_update();
		dest[TWI_REG_SENSITIVITY] = (uint8_t)sensitivity;
		dest[TWI_REG_SENSITIVITY + 1U] = (uint8_t)(sensitivity >> 8);
		regs_pub = dest;
	}
}

void twi_slave_set_hv_command(uint8_t on)
{
	twi_put(TWI_REG_HV_ENABLE, on ? 1U : 0U);
}

void twi_slave_set_hv_status(uint8_t status)
{
	twi_put(TWI_REG_HV_STATUS, status);
}

void twi_slave_set_led(uint8_t on)
{
	twi_put(TWI_REG_LED, on ? 1U : 0U);
}

void twi_slave_set_lp(uint8_t on)
{
	twi_put(TWI_REG_LP_MODE, on ? 1U : 0U);
}

#endif

uint8_t twi_slave_take_write(struct twi_write_cmd *out)
{
	uint8_t ready;
	uint8_t i;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		ready = twi_cmd_ready;
		if (ready) {
			out->ptr = twi_cmd_ptr;
			out->len = twi_cmd_len;
			for (i = 0; i < TWI_WRITE_MAX; i++) {
				out->data[i] = twi_cmd_data[i];
			}
			twi_cmd_ready = 0;
		}
	}
	return ready;
}
