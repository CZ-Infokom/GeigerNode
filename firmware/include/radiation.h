#ifndef RADIATION_H
#define RADIATION_H

#include <stdint.h>

/*
 * Accepted SI-29BG factory Cs-137 sensitivity, 137.5 impulses/uR.
 * The x10 representation keeps the half impulse without floating point.
 * The RadSens integer register is the nearest whole impulse/uR.
 */
#define RADIATION_SI29BG_SENSITIVITY_X10 1375U
#define RADIATION_SI29BG_SENSITIVITY_REG 138U
#define RADIATION_RADSENS_MAX            0x00FFFFFFUL

/*
 * Convert a raw count accumulated over elapsed_ms to RadSens static
 * radiation units of 0.1 uR/h. The result is rounded to nearest and
 * saturated to the 24-bit register representation.
 */
uint32_t radiation_counts_to_urh_x10(uint32_t count, uint32_t elapsed_ms);

#endif
