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
