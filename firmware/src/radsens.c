#include <avr/eeprom.h>
#include <stdint.h>

#include "radiation.h"
#include "radsens.h"
#include "twi_slave.h"

/*
 * Address and sensitivity survive reset. HV, the LED enable, and the
 * low-power bit do not: the datasheet only names those two as stored.
 * The record sits past the HV series at 0 (178 bytes) and the GM record
 * at 256 (106 bytes). make flash chip-erases EEPROM, which restores
 * address 0x66 and sensitivity 138.
 */
#define RADSENS_NVM_ADDR  960U
#define RADSENS_NVM_MAGIC 0xA5U

struct radsens_nvm {
	uint8_t magic;
	uint8_t address;
	uint8_t sens_lo;
	uint8_t sens_hi;
	uint8_t checksum;
};

_Static_assert(sizeof(struct radsens_nvm) == 5U, "RadSens NVM record is 5 bytes");
_Static_assert(RADSENS_NVM_ADDR >= 178U, "RadSens NVM clears the HV series");
_Static_assert(RADSENS_NVM_ADDR >= 256U + 106U, "RadSens NVM clears the GM record");
_Static_assert(RADSENS_NVM_ADDR + sizeof(struct radsens_nvm) <= 1024U,
	       "RadSens NVM fits in EEPROM");

static uint8_t device_address;
static uint16_t sensitivity;
static uint8_t hv_command;
static uint8_t led_enabled;

static uint8_t nvm_sum(const struct radsens_nvm *nvm)
{
	return (uint8_t)(nvm->magic + nvm->address + nvm->sens_lo + nvm->sens_hi);
}

static uint8_t nvm_valid(const struct radsens_nvm *nvm, uint16_t *sens_out)
{
	uint16_t sens;

	if (nvm->magic != RADSENS_NVM_MAGIC || nvm_sum(nvm) != nvm->checksum) {
		return 0;
	}
	if (nvm->address < 0x03U || nvm->address > 0x77U) {
		return 0;
	}
	sens = (uint16_t)nvm->sens_lo | ((uint16_t)nvm->sens_hi << 8);
	if (sens > RADIATION_SENSITIVITY_MAX) {
		return 0;
	}
	*sens_out = sens;
	return 1;
}

static void nvm_save(void)
{
	struct radsens_nvm nvm;

	nvm.magic = RADSENS_NVM_MAGIC;
	nvm.address = device_address;
	nvm.sens_lo = (uint8_t)sensitivity;
	nvm.sens_hi = (uint8_t)(sensitivity >> 8);
	nvm.checksum = nvm_sum(&nvm);
	eeprom_update_block(&nvm, (void *)RADSENS_NVM_ADDR, sizeof nvm);
}

static void apply_sensitivity(uint8_t ptr, const uint8_t *data, uint8_t len)
{
	uint16_t next = sensitivity;

	if (ptr == TWI_REG_SENSITIVITY && len >= 2U) {
		next = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
	} else if (ptr == TWI_REG_SENSITIVITY && len == 1U) {
		next = (uint16_t)((sensitivity & 0xFF00U) | data[0]);
	} else if (ptr == (uint8_t)(TWI_REG_SENSITIVITY + 1U) && len >= 1U) {
		next = (uint16_t)((sensitivity & 0x00FFU) | ((uint16_t)data[0] << 8));
	} else {
		return;
	}
	if (next > RADIATION_SENSITIVITY_MAX || next == sensitivity) {
		return;
	}
	sensitivity = next;
	twi_slave_set_sensitivity(sensitivity);
	nvm_save();
}

static void apply_write(const struct twi_write_cmd *cmd)
{
	uint8_t value;

	if (cmd->len == 0U) {
		return;
	}
	value = cmd->data[0];
	if (cmd->ptr == TWI_REG_LP_MODE) {
		if (value <= 1U) {
			twi_slave_set_lp(value);
		}
		return;
	}
	if (cmd->ptr == TWI_REG_ADDRESS) {
		if (value < 0x03U || value > 0x77U || value == device_address) {
			return;
		}
		device_address = value;
		twi_slave_set_address(device_address);
		nvm_save();
		return;
	}
	if (cmd->ptr == TWI_REG_HV_ENABLE) {
		if (value > 1U || value == hv_command) {
			return;
		}
		hv_command = value;
		twi_slave_set_hv_command(hv_command);
		return;
	}
	if (cmd->ptr == TWI_REG_SENSITIVITY ||
	    cmd->ptr == (uint8_t)(TWI_REG_SENSITIVITY + 1U)) {
		apply_sensitivity(cmd->ptr, cmd->data, cmd->len);
		return;
	}
	if (cmd->ptr == TWI_REG_LED) {
		if (value <= 1U) {
			led_enabled = value;
			twi_slave_set_led(led_enabled);
		}
	}
}

void radsens_init(void)
{
	struct radsens_nvm nvm;
	uint16_t loaded = RADIATION_SI29BG_SENSITIVITY_REG;

	device_address = TWI_SLAVE_ADDRESS;
	sensitivity = RADIATION_SI29BG_SENSITIVITY_REG;
	hv_command = 1U;
	led_enabled = 1U;

	eeprom_read_block(&nvm, (const void *)RADSENS_NVM_ADDR, sizeof nvm);
	if (nvm_valid(&nvm, &loaded)) {
		device_address = nvm.address;
		sensitivity = loaded;
	}

	twi_slave_set_address(device_address);
	twi_slave_set_sensitivity(sensitivity);
	twi_slave_set_hv_command(hv_command);
	twi_slave_set_led(led_enabled);
	twi_slave_set_lp(0);
	radsens_publish_radiation(0, 0);
}

void radsens_poll(void)
{
	struct twi_write_cmd cmd;

	while (twi_slave_take_write(&cmd)) {
		apply_write(&cmd);
	}
}

void radsens_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10)
{
	twi_slave_publish_radiation(dynamic_x10, static_x10);
}

void radsens_on_fault(void)
{
	hv_command = 0U;
	twi_slave_set_hv_command(0);
}

void radsens_set_hv_status(uint8_t state, uint8_t attempts)
{
	if (state > RADSENS_HV_GIVEN_UP) {
		state = RADSENS_HV_GIVEN_UP;
	}
	if (attempts > 15U) {
		attempts = 15U;
	}
	twi_slave_set_hv_status((uint8_t)((attempts << 4) | state));
}

uint8_t radsens_hv_command(void)
{
	return hv_command;
}

uint8_t radsens_led_enabled(void)
{
	return led_enabled;
}

uint16_t radsens_sensitivity(void)
{
	return sensitivity;
}
