#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "radiation.h"

static unsigned failures;

static void expect_u32(const char *name, uint32_t got, uint32_t want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: got %lu want %lu\n",
			name, (unsigned long)got, (unsigned long)want);
		failures++;
	}
}

static void expect_u8(const char *name, uint8_t got, uint8_t want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: got %u want %u\n",
			name, (unsigned)got, (unsigned)want);
		failures++;
	}
}

int main(void)
{
	uint8_t overflow = 0xFFU;
	uint32_t cpm;
	uint32_t dose;

	cpm = radiation_cpm_x10(90UL, 500000UL, &overflow);
	expect_u32("cpm_x10 90/500000", cpm, 108UL);
	expect_u8("cpm overflow clear", overflow, 0);

	dose = radiation_dose_nsvh(90UL, 500000UL,
				   RADIATION_SBM20_NSVH_PER_CPM_Q16,
				   &overflow);
	expect_u32("dose 90/500000", dose, 100UL);
	expect_u8("dose overflow clear", overflow, 0);

	overflow = 0xFFU;
	expect_u32("cpm elapsed 0", radiation_cpm_x10(90UL, 0UL, &overflow), 0UL);
	expect_u8("cpm elapsed 0 overflow", overflow, 0);

	overflow = 0xFFU;
	expect_u32("dose elapsed 0",
		   radiation_dose_nsvh(90UL, 0UL,
				       RADIATION_SBM20_NSVH_PER_CPM_Q16,
				       &overflow),
		   0UL);
	expect_u8("dose elapsed 0 overflow", overflow, 0);

	overflow = 0;
	cpm = radiation_cpm_x10(0xFFFFFFFFUL, 1UL, &overflow);
	expect_u32("cpm saturate", cpm, 0xFFFFFFFFUL);
	expect_u8("cpm saturate flag", overflow, 1);

	overflow = 0;
	dose = radiation_dose_nsvh(0xFFFFFFFFUL, 1UL, 0xFFFFFFFFUL, &overflow);
	expect_u32("dose intermediate overflow", dose, 0xFFFFFFFFUL);
	expect_u8("dose intermediate flag", overflow, 1);

	if (failures != 0U) {
		fprintf(stderr, "%u check(s) failed\n", failures);
		return 1;
	}
	puts("radiation_math_test: ok");
	return 0;
}
