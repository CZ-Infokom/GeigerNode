#include <stdint.h>

#include "radiation.h"

uint32_t radiation_counts_to_urh_x10(uint32_t count, uint32_t elapsed_ms)
{
	uint64_t denominator;
	uint64_t value;

	if (elapsed_ms == 0UL) {
		return 0;
	}

	/*
	 * CPM = count * 60000 / elapsed_ms
	 * uR/h = CPM * 60 / sensitivity
	 * RadSens raw = uR/h * 10
	 *
	 * Sensitivity is stored x10, so:
	 * raw = count * 360000000 / (elapsed_ms * sensitivity_x10)
	 */
	denominator = (uint64_t)elapsed_ms * RADIATION_SI29BG_SENSITIVITY_X10;
	value = ((uint64_t)count * 360000000ULL + denominator / 2ULL) /
		denominator;
	if (value > RADIATION_RADSENS_MAX) {
		return RADIATION_RADSENS_MAX;
	}
	return (uint32_t)value;
}
