#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "board.h"
#include "gm_counter.h"

/*
 * Every falling edge is counted. There is no dead time and no debounce.
 * Timer0, prescaler 8, is a 1 us free-running clock. Timer1 stays the HV
 * switcher and Timer2 stays the 1 ms tick. The overflow word plus TCNT0
 * is the stamp. A gap of 0 us is two edges in the same microsecond and
 * still counts, in the <50 us bin.
 *
 * Bins are exclusive. 2400 Hz switching is 417 us, so that spacing lands
 * in 200–500 us. A repeatable 100–300 us cluster is the tube or the
 * pulse path. Sensitivity is not applied here.
 */
#if F_CPU != 8000000UL
#error "The 1 us stamp is calculated for F_CPU=8000000UL"
#endif
_Static_assert(F_CPU / 8UL == 1000000UL, "Timer0 prescaler 8 is 1 us");
_Static_assert(GM_BLANKING_US == 0U, "software dead time stays off");

const struct gm_tube_profile gm_tube_si29bg __attribute__((used)) = {
	GM_TUBE_SI29BG_HV_V,
	RADIATION_SI29BG_SENSITIVITY_X10
};

static volatile uint32_t t0_ovf;
static volatile uint32_t gm_total_count;
static volatile uint32_t gm_window_count;
static volatile uint32_t gm_last_pulse_us;
static volatile uint32_t gm_min_interval_us;
static volatile uint16_t gm_bin_lt_50us;
static volatile uint16_t gm_bin_50_100us;
static volatile uint16_t gm_bin_100_200us;
static volatile uint16_t gm_bin_200_500us;
static volatile uint16_t gm_bin_500us_1ms;
static volatile uint16_t gm_bin_1_2ms;
static volatile uint16_t gm_bin_2_5ms;
static volatile uint16_t gm_bin_5_10ms;
static volatile uint16_t gm_bin_ge_10ms;
static volatile uint16_t gm_close_us[GM_CLOSE_LOG];
static volatile uint8_t gm_close_count;
static volatile uint8_t gm_pulse_seen;
static volatile uint8_t gm_window_open;
static volatile uint8_t gm_have_last;

static inline __attribute__((always_inline)) void gm_bump(volatile uint16_t *bin)
{
	if (*bin != 65535U) {
		(*bin)++;
	}
}

static void gm_clear_close(void)
{
	uint8_t i;

	for (i = 0; i < GM_CLOSE_LOG; i++) {
		gm_close_us[i] = 0;
	}
	gm_close_count = 0;
}

ISR(TIMER0_OVF_vect)
{
	t0_ovf++;
}

/* Called only from INT0, so Timer0's overflow ISR cannot run mid-read. */
static inline __attribute__((always_inline)) uint32_t gm_now_us(void)
{
	uint8_t low = TCNT0;
	uint32_t high = t0_ovf;

	if ((TIFR0 & _BV(TOV0)) && low < 0x80U) {
		high++;
	}
	return (high << 8) | low;
}

static inline __attribute__((always_inline)) void gm_bin_interval(uint32_t delta)
{
	if (delta < 50UL) {
		gm_bump(&gm_bin_lt_50us);
	} else if (delta < 100UL) {
		gm_bump(&gm_bin_50_100us);
	} else if (delta < 200UL) {
		gm_bump(&gm_bin_100_200us);
	} else if (delta < 500UL) {
		gm_bump(&gm_bin_200_500us);
	} else if (delta < 1000UL) {
		gm_bump(&gm_bin_500us_1ms);
	} else if (delta < 2000UL) {
		gm_bump(&gm_bin_1_2ms);
	} else if (delta < 5000UL) {
		gm_bump(&gm_bin_2_5ms);
	} else if (delta < 10000UL) {
		gm_bump(&gm_bin_5_10ms);
	} else {
		gm_bump(&gm_bin_ge_10ms);
	}
}

ISR(INT0_vect)
{
	uint32_t now;
	uint32_t delta;

	gm_total_count++;
	gm_pulse_seen = 1;
	if (!gm_window_open) {
		return;
	}
	gm_window_count++;
	now = gm_now_us();
	if (gm_have_last) {
		delta = now - gm_last_pulse_us;
		if (delta < gm_min_interval_us) {
			gm_min_interval_us = delta;
		}
		gm_bin_interval(delta);
		if (delta < GM_CLOSE_LIMIT_US && gm_close_count < GM_CLOSE_LOG) {
			gm_close_us[gm_close_count] = (uint16_t)delta;
			gm_close_count++;
		}
	} else {
		gm_have_last = 1;
	}
	gm_last_pulse_us = now;
}

uint16_t gm_tube_nominal_hv_v(void)
{
	return gm_tube_si29bg.nominal_hv_v;
}

void gm_counter_init(void)
{
	gm_total_count = 0;
	gm_window_count = 0;
	gm_last_pulse_us = 0;
	gm_min_interval_us = 0xFFFFFFFFUL;
	gm_bin_lt_50us = 0;
	gm_bin_50_100us = 0;
	gm_bin_100_200us = 0;
	gm_bin_200_500us = 0;
	gm_bin_500us_1ms = 0;
	gm_bin_1_2ms = 0;
	gm_bin_2_5ms = 0;
	gm_bin_5_10ms = 0;
	gm_bin_ge_10ms = 0;
	gm_clear_close();
	gm_pulse_seen = 0;
	gm_window_open = 0;
	gm_have_last = 0;

	/* Input with pull-up. Idle is high. A discharge is a falling edge. */
	GM_PULSE_DDR &= ~_BV(GM_PULSE_DDR_BIT);
	GM_PULSE_PORT |= _BV(GM_PULSE_PORT_BIT);

	EICRA = (EICRA & ~(_BV(ISC01) | _BV(ISC00))) | _BV(ISC01);
	EIFR = _BV(INTF0);
	EIMSK |= _BV(INT0);

	/*
	 * Normal mode, prescaler 8, overflow only. Compare outputs stay
	 * disconnected so PD5 and PD6 are not driven.
	 */
	TCCR0A = 0;
	TCCR0B = 0;
	TCNT0 = 0;
	t0_ovf = 0;
	TIFR0 = _BV(TOV0);
	TIMSK0 = _BV(TOIE0);
	TCCR0B = _BV(CS01);
}

void gm_counter_open_window(void)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		gm_window_count = 0;
		gm_min_interval_us = 0xFFFFFFFFUL;
		gm_bin_lt_50us = 0;
		gm_bin_50_100us = 0;
		gm_bin_100_200us = 0;
		gm_bin_200_500us = 0;
		gm_bin_500us_1ms = 0;
		gm_bin_1_2ms = 0;
		gm_bin_2_5ms = 0;
		gm_bin_5_10ms = 0;
		gm_bin_ge_10ms = 0;
		gm_clear_close();
		gm_have_last = 0;
		gm_last_pulse_us = 0;
		gm_window_open = 1;
	}
}

void gm_counter_close_window(void)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		gm_window_open = 0;
	}
}

uint32_t gm_counter_window(void)
{
	uint32_t count;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		count = gm_window_count;
	}
	return count;
}

uint32_t gm_counter_total(void)
{
	uint32_t count;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		count = gm_total_count;
	}
	return count;
}

void gm_counter_snapshot(struct gm_counter_snapshot *out)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		out->total_count = gm_total_count;
		out->window_count = gm_window_count;
		out->min_interval_us = gm_min_interval_us;
		out->bin_lt_50us = gm_bin_lt_50us;
		out->bin_50_100us = gm_bin_50_100us;
		out->bin_100_200us = gm_bin_100_200us;
		out->bin_200_500us = gm_bin_200_500us;
		out->bin_500us_1ms = gm_bin_500us_1ms;
		out->bin_1_2ms = gm_bin_1_2ms;
		out->bin_2_5ms = gm_bin_2_5ms;
		out->bin_5_10ms = gm_bin_5_10ms;
		out->bin_ge_10ms = gm_bin_ge_10ms;
		out->close_interval_count = gm_close_count;
		for (uint8_t i = 0; i < GM_CLOSE_LOG; i++) {
			out->close_intervals_us[i] = gm_close_us[i];
		}
	}
}

uint8_t gm_counter_take_pulse(void)
{
	uint8_t seen;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		seen = gm_pulse_seen;
		gm_pulse_seen = 0;
	}
	return seen;
}
