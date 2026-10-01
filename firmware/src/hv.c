#include <avr/io.h>
#include <stdint.h>

#include "adc.h"
#include "board.h"
#include "hv.h"

#if F_CPU != 8000000UL
#error "HV pulse counts are calculated for F_CPU=8000000UL"
#endif

#define HV_ON_COUNTS     ((F_CPU / 1000000UL) * HV_PULSE_US)
#define HV_MAX_ON_COUNTS ((F_CPU / 1000000UL) * HV_HARD_MAX_PULSE_US)
#define HV_PERIOD_COUNTS (F_CPU / HV_REP_HZ)

#if HV_PULSE_US > HV_HARD_MAX_PULSE_US
#error "HV pulse is wider than the hard maximum"
#endif
#if HV_ON_COUNTS == 0 || HV_ON_COUNTS > HV_MAX_ON_COUNTS
#error "HV on-count is outside the hard maximum"
#endif
#if HV_PERIOD_COUNTS <= HV_MAX_ON_COUNTS || HV_PERIOD_COUNTS > 65535UL
#error "HV period does not fit Timer1"
#endif
#if HV_REP_HZ < 500 || HV_REP_HZ > 1000
#error "HV repetition is outside the open-loop characterization region"
#endif

static uint8_t faulted;
static uint16_t started_ms;

static uint16_t hv_on_counts(void)
{
	uint16_t on = HV_ON_COUNTS;

	if (on > HV_MAX_ON_COUNTS) {
		on = HV_MAX_ON_COUNTS;
	}
	return on;
}

void hv_force_off(void)
{
	/* Disconnect OC1A before relying on the PORT bit. */
	TCCR1A = 0;
	TCCR1B = 0;
	HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
	HV_SW_DDR |= _BV(HV_SW_DDR_BIT);
	faulted = 1;
}

uint8_t hv_is_faulted(void)
{
	return faulted;
}

void hv_start(uint16_t now_ms)
{
	if (faulted) {
		hv_force_off();
		return;
	}

	started_ms = now_ms;
	TCCR1A = 0;
	TCCR1B = 0;
	TCNT1 = 0;
	ICR1 = HV_PERIOD_COUNTS - 1;
	OCR1A = hv_on_counts();
	/* Fast PWM mode 14, TOP = ICR1. OC1A clears at OCR1A and sets at BOTTOM. */
	TCCR1A = _BV(COM1A1) | _BV(WGM11);
	TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
}

void hv_poll(uint16_t now_ms)
{
	uint32_t node_uv;

	if (faulted) {
		return;
	}
	if ((uint16_t)(now_ms - started_ms) >= HV_RUN_MS) {
		hv_force_off();
		return;
	}

	node_uv = hv_adc_to_uv(adc_read_hv_raw(), ADC_VREF_NOMINAL_UV);
	if (node_uv >= HV_FAULT_NODE_UV) {
		hv_force_off();
	}
}
