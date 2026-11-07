#ifdef GEIGERNODE_PROTOCOL_NATIVE

#include <avr/eeprom.h>
#include <stdint.h>

#include "gm_counter.h"
#include "gn_config.h"
#include "gn_protocol.h"
#include "radiation.h"

/*
 * Same EEPROM address as the RadSens record. The magic byte keeps one
 * image from adopting the other's bytes. 960 sits past the HV series
 * at 0 (178 bytes) and the GM record at 256 (106 bytes).
 */
#define GN_NVM_ADDR     960U
#define GN_NVM_MAGIC    0xC3U
#define GN_NVM_VERSION  1U

struct gn_nvm {
	uint8_t magic;
	uint8_t version;
	uint8_t window_hi;
	uint8_t window_lo;
	uint8_t holdoff_hi;
	uint8_t holdoff_lo;
	uint8_t cal0;
	uint8_t cal1;
	uint8_t cal2;
	uint8_t cal3;
	uint8_t checksum;
};

_Static_assert(sizeof(struct gn_nvm) == 11U, "native config record is 11 bytes");
_Static_assert(GN_NVM_ADDR >= 178U, "native config clears the HV series");
_Static_assert(GN_NVM_ADDR >= 256U + 106U, "native config clears the GM record");
_Static_assert(GN_NVM_ADDR + sizeof(struct gn_nvm) <= 1024U,
	       "native config fits in EEPROM");
_Static_assert(GN_NVM_MAGIC != 0xA5U, "native magic differs from RadSens");
_Static_assert(GN_HOLDOFF_MAX_US == GM_BLANKING_US, "holdoff ceiling is 300 us");
_Static_assert(GN_HOLDOFF_DEFAULT_US == GM_BLANKING_US, "holdoff default is 300 us");
_Static_assert(GN_WINDOW_DEFAULT_S == 300U, "window default is 300 s");
_Static_assert(GN_WINDOW_DEFAULT_S <= GN_WINDOW_MAX_S, "default window fits");
_Static_assert(GN_WINDOW_MIN_S >= 1U, "window is at least one second");

static uint16_t cfg_window = GN_WINDOW_DEFAULT_S;
static uint16_t cfg_holdoff = GN_HOLDOFF_DEFAULT_US;
static uint32_t cfg_cal = RADIATION_SBM20_NSVH_PER_CPM_Q16;

static uint8_t nvm_sum(const struct gn_nvm *nvm)
{
	return (uint8_t)(nvm->magic + nvm->version + nvm->window_hi +
			 nvm->window_lo + nvm->holdoff_hi + nvm->holdoff_lo +
			 nvm->cal0 + nvm->cal1 + nvm->cal2 + nvm->cal3);
}

static uint8_t cfg_in_range(uint16_t window_s, uint16_t holdoff_us, uint32_t cal)
{
	if (window_s < GN_WINDOW_MIN_S || window_s > GN_WINDOW_MAX_S) {
		return 0;
	}
	if (holdoff_us > GN_HOLDOFF_MAX_US) {
		return 0;
	}
	return cal != 0U;
}

static void cfg_publish(void)
{
	twi_slave_set_native_config(cfg_window, cfg_holdoff, cfg_cal);
	gm_counter_set_blanking_us(cfg_holdoff);
}

static void cfg_clear_histogram(void)
{
	gm_counter_clear_histogram();
	twi_slave_clear_native_histogram();
}

static void nvm_save(void)
{
	struct gn_nvm nvm;

	nvm.magic = GN_NVM_MAGIC;
	nvm.version = GN_NVM_VERSION;
	nvm.window_hi = (uint8_t)(cfg_window >> 8);
	nvm.window_lo = (uint8_t)cfg_window;
	nvm.holdoff_hi = (uint8_t)(cfg_holdoff >> 8);
	nvm.holdoff_lo = (uint8_t)cfg_holdoff;
	nvm.cal0 = (uint8_t)(cfg_cal >> 24);
	nvm.cal1 = (uint8_t)(cfg_cal >> 16);
	nvm.cal2 = (uint8_t)(cfg_cal >> 8);
	nvm.cal3 = (uint8_t)cfg_cal;
	nvm.checksum = nvm_sum(&nvm);
	eeprom_update_block(&nvm, (void *)GN_NVM_ADDR, sizeof nvm);
}

static uint16_t cfg_u16(uint8_t hi, uint8_t lo)
{
	return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static uint32_t cfg_cal_bytes(const struct gn_nvm *nvm)
{
	return ((uint32_t)nvm->cal0 << 24) | ((uint32_t)nvm->cal1 << 16) |
	       ((uint32_t)nvm->cal2 << 8) | nvm->cal3;
}

void gn_config_init(void)
{
	struct gn_nvm nvm;
	uint16_t window_s;
	uint16_t holdoff_us;
	uint32_t cal;

	eeprom_read_block(&nvm, (const void *)GN_NVM_ADDR, sizeof nvm);
	window_s = cfg_u16(nvm.window_hi, nvm.window_lo);
	holdoff_us = cfg_u16(nvm.holdoff_hi, nvm.holdoff_lo);
	cal = cfg_cal_bytes(&nvm);
	if (nvm.magic == GN_NVM_MAGIC && nvm.version == GN_NVM_VERSION &&
	    nvm_sum(&nvm) == nvm.checksum &&
	    cfg_in_range(window_s, holdoff_us, cal)) {
		cfg_window = window_s;
		cfg_holdoff = holdoff_us;
		cfg_cal = cal;
	}
	cfg_publish();
}

void gn_config_on_write(const struct twi_write_cmd *cmd)
{
	uint16_t value;
	uint32_t cal;

	if (cmd->ptr == GN_REG_HISTOGRAM_FLAGS && cmd->len == 1U) {
		if (cmd->data[0] == GN_HISTOGRAM_CLEAR) {
			cfg_clear_histogram();
		}
		return;
	}
	if (cmd->ptr == GN_REG_CAL && cmd->len == 4U) {
		cal = ((uint32_t)cmd->data[0] << 24) |
		      ((uint32_t)cmd->data[1] << 16) |
		      ((uint32_t)cmd->data[2] << 8) | cmd->data[3];
		if (cal == 0U || cal == cfg_cal) {
			return;
		}
		cfg_cal = cal;
		nvm_save();
		cfg_publish();
		return;
	}
	if (cmd->len != 2U) {
		return;
	}
	value = cfg_u16(cmd->data[0], cmd->data[1]);
	if (cmd->ptr == GN_REG_WINDOW_S) {
		if (value < GN_WINDOW_MIN_S || value > GN_WINDOW_MAX_S ||
		    value == cfg_window) {
			return;
		}
		cfg_window = value;
		nvm_save();
		cfg_publish();
		return;
	}
	if (cmd->ptr == GN_REG_HOLDOFF_US) {
		if (value > GN_HOLDOFF_MAX_US || value == cfg_holdoff) {
			return;
		}
		cfg_holdoff = value;
		nvm_save();
		cfg_publish();
		cfg_clear_histogram();
	}
}

uint16_t gn_config_window_s(void)
{
	return cfg_window;
}

uint16_t gn_config_holdoff_us(void)
{
	return cfg_holdoff;
}

uint32_t gn_config_cal_q16(void)
{
	return cfg_cal;
}

#endif
