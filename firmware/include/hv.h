#ifndef HV_H
#define HV_H

#include <stdint.h>

/*
 * Open-loop bring-up pulse on PB1 / OC1A.
 * 25 us high, repeated at 500 Hz. The hardware compare match is the
 * pulse-width limit. Nothing in the regulator path can widen it.
 */
#define HV_PULSE_US            25U
#define HV_HARD_MAX_PULSE_US   100U
#define HV_REP_HZ              500U

/* Nominal HV-node ceiling. The 1.1 V reference is not calibrated. */
#define HV_FAULT_NODE_UV       150000000UL

/* Pulse train length for the first scope check, then PB1 latches low. */
#define HV_RUN_MS              10000U

void hv_start(uint16_t now_ms);
void hv_poll(uint16_t now_ms);
void hv_force_off(void);
uint8_t hv_is_faulted(void);

#endif
