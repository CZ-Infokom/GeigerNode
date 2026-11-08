#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "board.h"
#include "gm_counter.h"

/*
 * Accepted falling edges are counted. For the live holdoff after an
 * accepted edge, further falling edges are ignored and do not move the
 * stamp. The live value starts at GM_BLANKING_DEFAULT_US and never
 * exceeds GM_BLANKING_US.
 * Timer0, prescaler 8, is a 1 us free-running clock. Timer1 stays the HV
 * switcher and Timer2 stays the 1 ms tick. The overflow word plus TCNT0
 * is the stamp.
 *
 * Bins are exclusive and count accepted gaps only. Holdoff 0 keeps the
 * classic upper bounds 50..10000 us. Any other holdoff starts bin 0 at
 * that holdoff and stretches those bounds up to 10 ms. 2400 Hz switching
 * is 417 us. Sensitivity is not applied here.
 */
#if F_CPU != 8000000UL
#error "The 1 us stamp is calculated for F_CPU=8000000UL"
#endif
_Static_assert(F_CPU / 8UL == 1000000UL, "Timer0 prescaler 8 is 1 us");
_Static_assert(GM_BLANKING_US == 300U, "holdoff ceiling is 300 us");
_Static_assert(GM_BLANKING_DEFAULT_US == 200U, "holdoff default is 200 us");
_Static_assert(GM_BLANKING_DEFAULT_US <= GM_BLANKING_US,
	       "default holdoff fits the ceiling");

const struct gm_tube_profile gm_tube_si29bg __attribute__((used)) = {
	GM_TUBE_SI29BG_HV_V,
	RADIATION_SI29BG_SENSITIVITY_X10
};

const struct gm_tube_profile gm_tube_sbm20 = {
	GM_TUBE_SBM20_HV_V,
	RADIATION_SBM20_SENSITIVITY_X10
};

const struct gm_tube_profile *const gm_tube_active = &gm_tube_sbm20;

static volatile uint32_t t0_ovf;
static volatile uint32_t gm_total_count;
static volatile uint32_t gm_window_count;
static volatile uint32_t gm_last_pulse_us;
static volatile uint32_t gm_min_interval_us;
static volatile uint16_t gm_bins[GM_HISTOGRAM_BIN_COUNT];
static volatile uint16_t gm_edges[GM_HISTOGRAM_EDGE_COUNT];
static volatile uint16_t gm_close_us[GM_CLOSE_LOG];
static volatile uint8_t gm_close_count;
static volatile uint8_t gm_pulse_seen;
static volatile uint8_t gm_window_open;
static volatile uint8_t gm_have_last;
static volatile uint16_t gm_blanking_us = GM_BLANKING_DEFAULT_US;

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

static const uint16_t gm_classic_edges[GM_HISTOGRAM_EDGE_COUNT] = {
	50U, 100U, 200U, 500U, 1000U, 2000U, 5000U, GM_HISTOGRAM_SPAN_US
};

_Static_assert(GM_HISTOGRAM_EDGE_COUNT + 1U == GM_HISTOGRAM_BIN_COUNT,
	       "eight upper bounds make nine bins");
_Static_assert(GM_HISTOGRAM_SPAN_US == 10000U, "open bin starts at 10 ms");

/* Call with INT0 blocked, or before EIMSK is set. */
static void gm_zero_histogram(void)
{
	uint8_t i;

	gm_min_interval_us = 0xFFFFFFFFUL;
	for (i = 0; i < GM_HISTOGRAM_BIN_COUNT; i++) {
		gm_bins[i] = 0;
	}
	gm_clear_close();
}

/* Call with INT0 blocked, or before EIMSK is set. */
static void gm_install_edges(uint16_t holdoff_us)
{
	uint8_t i;
	uint16_t prev;

	if (holdoff_us == 0U) {
		for (i = 0; i < GM_HISTOGRAM_EDGE_COUNT; i++) {
			gm_edges[i] = gm_classic_edges[i];
		}
		return;
	}
	prev = holdoff_us;
	for (i = 0; i < GM_HISTOGRAM_EDGE_COUNT; i++) {
		uint32_t num;
		uint16_t edge;

		num = (uint32_t)gm_classic_edges[i] *
		      (uint32_t)(GM_HISTOGRAM_SPAN_US - holdoff_us) + 5000UL;
		edge = (uint16_t)(holdoff_us + (num / (uint32_t)GM_HISTOGRAM_SPAN_US));
		if (i == (GM_HISTOGRAM_EDGE_COUNT - 1U)) {
			edge = GM_HISTOGRAM_SPAN_US;
		}
		if (edge <= prev) {
			edge = (uint16_t)(prev + 1U);
		}
		gm_edges[i] = edge;
		prev = edge;
	}
	gm_edges[GM_HISTOGRAM_EDGE_COUNT - 1U] = GM_HISTOGRAM_SPAN_US;
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
	uint8_t i;

	for (i = 0; i < GM_HISTOGRAM_EDGE_COUNT; i++) {
		if (delta < gm_edges[i]) {
			gm_bump(&gm_bins[i]);
			return;
		}
	}
	gm_bump(&gm_bins[GM_HISTOGRAM_BIN_COUNT - 1U]);
}

ISR(INT0_vect)
{
	uint32_t now;
	uint32_t delta = 0;
	uint8_t had_last;

	now = gm_now_us();
	had_last = gm_have_last;
	if (had_last) {
		delta = now - gm_last_pulse_us;
		if (delta < gm_blanking_us) {
			return;
		}
	}

	gm_total_count++;
	gm_pulse_seen = 1;
	gm_last_pulse_us = now;
	gm_have_last = 1;
	if (!gm_window_open) {
		return;
	}
	gm_window_count++;
	if (!had_last) {
		return;
	}
	if (delta < gm_min_interval_us) {
		gm_min_interval_us = delta;
	}
	gm_bin_interval(delta);
	if (delta < GM_CLOSE_LIMIT_US && gm_close_count < GM_CLOSE_LOG) {
		gm_close_us[gm_close_count] = (uint16_t)delta;
		gm_close_count++;
	}
}

uint16_t gm_tube_nominal_hv_v(void)
{
	return gm_tube_active->nominal_hv_v;
}

uint16_t gm_tube_sensitivity_reg(void)
{
	return (uint16_t)((gm_tube_active->sensitivity_imp_per_ur_x10 + 5U) / 10U);
}

void gm_counter_set_blanking_us(uint16_t blanking_us)
{
	if (blanking_us > GM_BLANKING_US) {
		blanking_us = GM_BLANKING_US;
	}
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		gm_blanking_us = blanking_us;
		gm_install_edges(blanking_us);
	}
}

void gm_counter_init(void)
{
	gm_blanking_us = GM_BLANKING_DEFAULT_US;
	gm_install_edges(GM_BLANKING_DEFAULT_US);
	gm_total_count = 0;
	gm_window_count = 0;
	gm_last_pulse_us = 0;
	gm_zero_histogram();
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
		gm_zero_histogram();
		gm_window_open = 1;
	}
}

void gm_counter_clear_histogram(void)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		gm_zero_histogram();
		gm_have_last = 0;
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
		out->bin_lt_50us = gm_bins[0];
		out->bin_50_100us = gm_bins[1];
		out->bin_100_200us = gm_bins[2];
		out->bin_200_500us = gm_bins[3];
		out->bin_500us_1ms = gm_bins[4];
		out->bin_1_2ms = gm_bins[5];
		out->bin_2_5ms = gm_bins[6];
		out->bin_5_10ms = gm_bins[7];
		out->bin_ge_10ms = gm_bins[8];
		out->close_interval_count = gm_close_count;
		for (uint8_t i = 0; i < GM_CLOSE_LOG; i++) {
			out->close_intervals_us[i] = gm_close_us[i];
		}
	}
}

void gm_counter_histogram(struct gm_histogram *out)
{
	uint8_t flags = 0;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		uint8_t i;

		out->min_interval_us = gm_min_interval_us;
		for (i = 0; i < GM_HISTOGRAM_BIN_COUNT; i++) {
			out->bins[i] = gm_bins[i];
		}
	}
	if (out->bins[0] == 65535U || out->bins[1] == 65535U ||
	    out->bins[2] == 65535U || out->bins[3] == 65535U ||
	    out->bins[4] == 65535U || out->bins[5] == 65535U ||
	    out->bins[6] == 65535U || out->bins[7] == 65535U ||
	    out->bins[8] == 65535U) {
		flags = GM_HISTOGRAM_SATURATED;
	}
	out->flags = flags;
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
