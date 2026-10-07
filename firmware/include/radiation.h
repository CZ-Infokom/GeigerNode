#ifndef RADIATION_H
#define RADIATION_H

#include <stdint.h>

/*
 * Factory SI-29BG Cs-137 sensitivity is 137.5 impulses/uR. The RadSens
 * register is a whole number, so the published default is 138. Dose math
 * uses the live register, which a host can change for another tube.
 * Pulse counting must not scale by this value.
 */
#define RADIATION_SI29BG_SENSITIVITY_X10 1375U
#define RADIATION_SI29BG_SENSITIVITY_REG 138U
#define RADIATION_SENSITIVITY_MAX        510U
/* Datasheet ceiling: 144000.0 uR/h in units of 0.1 uR/h. */
#define RADIATION_RADSENS_MAX            1440000UL

/*
 * Convert a raw count accumulated over elapsed_ms to RadSens units of
 * 0.1 uR/h. sensitivity_imp_per_ur is the integer register value.
 * The result is rounded to nearest. Zero elapsed time or zero
 * sensitivity yields 0. Values above the datasheet ceiling saturate.
 */
uint32_t radiation_counts_to_urh_x10(uint32_t count,
				     uint32_t elapsed_ms,
				     uint16_t sensitivity_imp_per_ur);

#endif
