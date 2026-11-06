#ifndef RADIATION_H
#define RADIATION_H

#include <stdint.h>

/*
 * Sensitivities are impulses/uR. The x10 form keeps a half-count exact.
 * The RadSens register is a whole number: (x10 + 5) / 10.
 * SI-29BG Cs-137 is 137.5, so its register value is 138.
 * The fitted tube is an SBM-20. Its Cs-137 datasheet midpoint is 65,
 * from 240-280 counts/s at 4 uR/s. Dose math uses the live register,
 * which a host can change. Pulse counting must not scale by it.
 * The RadSens-module Ra-226 default of 105 is not used.
 */
#define RADIATION_SI29BG_SENSITIVITY_X10 1375U
#define RADIATION_SI29BG_SENSITIVITY_REG 138U
#define RADIATION_SBM20_SENSITIVITY_X10  650U
#define RADIATION_SBM20_SENSITIVITY_REG  65U
#define RADIATION_SENSITIVITY_MAX        510U
/* Datasheet ceiling: 144000.0 uR/h in units of 0.1 uR/h. */
#define RADIATION_RADSENS_MAX            1440000UL

/*
 * Direct nSv/h per CPM, Q16.16. This is the SBM-20 Cs-137 midpoint
 * 65 impulses/uR expressed as 10 nSv/uR * 60 / 65, not a new fit
 * and not computed through uR/h at runtime:
 * 600/65 * 65536 = 39321600/65 = 604947.692... → 604948.
 */
#define RADIATION_SBM20_NSVH_PER_CPM_Q16 604948UL

/*
 * Convert a raw count accumulated over elapsed_ms to RadSens units of
 * 0.1 uR/h. sensitivity_imp_per_ur is the integer register value.
 * The result is rounded to nearest. Zero elapsed time or zero
 * sensitivity yields 0. Values above the datasheet ceiling saturate.
 */
uint32_t radiation_counts_to_urh_x10(uint32_t count,
				     uint32_t elapsed_ms,
				     uint16_t sensitivity_imp_per_ur);

/*
 * Native protocol rates from one observation window. overflow is set
 * when the result saturates at UINT32_MAX or an intermediate product
 * does not fit in 64 bits. Zero elapsed_ms yields 0 and does not set
 * overflow. cpm_x10 is round(count * 600000 / elapsed_ms). dose is
 * round(count * 60000 * cal_q16 / (elapsed_ms * 65536)).
 */
uint32_t radiation_cpm_x10(uint32_t count, uint32_t elapsed_ms,
			   uint8_t *overflow);
uint32_t radiation_dose_nsvh(uint32_t count, uint32_t elapsed_ms,
			     uint32_t cal_q16, uint8_t *overflow);

#endif
