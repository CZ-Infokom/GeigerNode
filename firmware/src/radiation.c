#include <stdint.h>

#include "radiation.h"

uint32_t radiation_counts_to_urh_x10(uint32_t count,
				     uint32_t elapsed_ms,
				     uint16_t sensitivity_imp_per_ur)
{
	uint64_t denominator;
	uint64_t value;

	if (elapsed_ms == 0UL || sensitivity_imp_per_ur == 0U) {
		return 0;
	}

	/*
	 * CPM = count * 60000 / elapsed_ms
	 * uR/h = CPM * 60 / S
	 * register = uR/h * 10
	 *         = count * 36000000 / (elapsed_ms * S)
	 */
	denominator = (uint64_t)elapsed_ms * sensitivity_imp_per_ur;
	value = ((uint64_t)count * 36000000ULL + denominator / 2ULL) /
		denominator;
	if (value > RADIATION_RADSENS_MAX) {
		return RADIATION_RADSENS_MAX;
	}
	return (uint32_t)value;
}

uint32_t radiation_cpm_x10(uint32_t count, uint32_t elapsed_ms,
			   uint8_t *overflow)
{
	uint64_t value;
	uint8_t ov = 0;

	if (elapsed_ms == 0UL) {
		if (overflow != 0) {
			*overflow = 0;
		}
		return 0;
	}

	/* round(count * 600000 / elapsed_ms). uint32 count * 600000 fits. */
	value = ((uint64_t)count * 600000ULL + (elapsed_ms / 2ULL)) /
		elapsed_ms;
	if (value > 0xFFFFFFFFULL) {
		ov = 1;
		value = 0xFFFFFFFFULL;
	}
	if (overflow != 0) {
		*overflow = ov;
	}
	return (uint32_t)value;
}

uint32_t radiation_dose_nsvh(uint32_t count, uint32_t elapsed_ms,
			     uint32_t cal_q16, uint8_t *overflow)
{
	uint64_t c60;
	uint64_t num;
	uint64_t denom;
	uint64_t value;

	if (elapsed_ms == 0UL) {
		if (overflow != 0) {
			*overflow = 0;
		}
		return 0;
	}

	/*
	 * round(count * 60000 * cal_q16 / (elapsed_ms * 65536))
	 * uses the same observation as radiation_cpm_x10.
	 */
	if (count > 0UL && 60000ULL > (UINT64_MAX / (uint64_t)count)) {
		if (overflow != 0) {
			*overflow = 1;
		}
		return 0xFFFFFFFFUL;
	}
	c60 = (uint64_t)count * 60000ULL;
	if (cal_q16 > 0UL && c60 > (UINT64_MAX / (uint64_t)cal_q16)) {
		if (overflow != 0) {
			*overflow = 1;
		}
		return 0xFFFFFFFFUL;
	}
	num = c60 * (uint64_t)cal_q16;
	denom = (uint64_t)elapsed_ms * 65536ULL;
	value = (num + denom / 2ULL) / denom;
	if (value > 0xFFFFFFFFULL) {
		if (overflow != 0) {
			*overflow = 1;
		}
		return 0xFFFFFFFFUL;
	}
	if (overflow != 0) {
		*overflow = 0;
	}
	return (uint32_t)value;
}
