#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "adc.h"
#include "gm_counter.h"
#include "gpio.h"
#include "hv.h"
#include "radiation.h"
#include "radsens.h"
#include "twi_slave.h"
#ifdef GEIGERNODE_PROTOCOL_NATIVE
#include "gn_config.h"
#endif

#if F_CPU != 8000000UL
#error "The 1 ms tick is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 CTC, prescaler 64, OCR2A 124. 8000000 / (64 * 125) = 1000 Hz.
 * The tick ISR only counts. INT0 owns the GM edges. Hardware TWI0
 * owns the bus. Main owns HV, dose conversion, and the LED.
 */
_Static_assert(F_CPU / 64UL / 125UL == 1000UL, "Timer2 CTC is 1 ms");
_Static_assert(RADIATION_SI29BG_SENSITIVITY_REG == 138U,
	       "SI-29BG register value remains 138 impulses/uR");
_Static_assert(RADIATION_SBM20_SENSITIVITY_REG == 65U,
	       "SBM-20 Cs-137 datasheet midpoint is 65 impulses/uR");
_Static_assert(RADIATION_SBM20_SENSITIVITY_X10 ==
	       RADIATION_SBM20_SENSITIVITY_REG * 10U,
	       "SBM-20 x10 field is 65.0 impulses/uR");
_Static_assert((RADIATION_SI29BG_SENSITIVITY_X10 + 5U) / 10U ==
	       RADIATION_SI29BG_SENSITIVITY_REG,
	       "SI-29BG register rounds 137.5 to 138");
_Static_assert(RADIATION_RADSENS_MAX == 1440000UL,
	       "RadSens intensity saturates at 144000.0 uR/h");
#ifdef GEIGERNODE_PROTOCOL_NATIVE
_Static_assert(TWI_SLAVE_ADDRESS == 0x67U, "native address is 0x67");
_Static_assert(TWI_REG_COUNT == 0x50U, "native map is 80 bytes");
_Static_assert(GN_PROTOCOL_VERSION == 2U, "protocol version is 2");
_Static_assert(GN_FW_MAJOR == 0U && GN_FW_MINOR == 2U &&
	       GN_FW_PATCH == 1U && GN_FW_PRE == 4U,
	       "firmware version is 0.2.1-beta.4");
_Static_assert(GN_CAPS_VALUE == 0x1FU, "caps include peaks");
_Static_assert(GN_HISTOGRAM_CLEAR == 0x01U, "histogram clear is write 1 at 0x38");
_Static_assert(GM_HISTOGRAM_BIN_COUNT == 9U, "nine exclusive bins");
_Static_assert(GM_HISTOGRAM_EDGE_COUNT == 8U, "eight holdoff-scaled upper bounds");
_Static_assert(GN_PEAK_SPAN_DEFAULT_S == 10U, "peak span default is 10 s");
_Static_assert(RADIATION_SBM20_NSVH_PER_CPM_Q16 == 604948UL,
	       "SBM-20 Q16.16 is round(600/65*65536)");
#else
_Static_assert(TWI_REG_COUNT > TWI_REG_LED, "RadSens map includes 0x14");
#endif

#define APP_PRE_HV_MS       500UL
#define APP_PUBLISH_MS      1000UL
/*
 * History ring. Native can publish the last 1..400 s; the default is
 * 300 s. RadSens publishes this whole ring, fixed at 300 s.
 */
#ifdef GEIGERNODE_PROTOCOL_NATIVE
#define APP_STATIC_MAX      GN_WINDOW_MAX_S
#else
#define APP_STATIC_MAX      300U
#endif
#define APP_DYNAMIC_MAX     122U
#define APP_DYNAMIC_RECENT  10U
#define APP_SHRINK_COUNTS   4U
#define GM_PULSE_LED_MS     40UL

/*
 * Four starts. After an operational fault the switch stays off for 30 s,
 * then 2 min, then 10 min. The fourth failure latches off. Ten continuous
 * minutes in run clears the count. An illegal pulse or rate does not retry.
 */
#define APP_ATTEMPTS        4U
#define APP_COOLDOWN_1_MS   30000UL
#define APP_COOLDOWN_2_MS   120000UL
#define APP_COOLDOWN_3_MS   600000UL
#define APP_BUDGET_CLEAR_MS (10UL * 60UL * 1000UL)
#define APP_COOLDOWN_BLINK_MS 2000UL
#define APP_COOLDOWN_ON_MS    100UL

_Static_assert(APP_DYNAMIC_MAX < APP_STATIC_MAX, "dynamic window fits in the history ring");
_Static_assert(APP_DYNAMIC_RECENT < APP_DYNAMIC_MAX, "shrink leaves an older slice");
_Static_assert(APP_ATTEMPTS == 4U, "four HV starts before give-up");
_Static_assert(APP_COOLDOWN_1_MS < APP_COOLDOWN_2_MS, "first cooldown is shortest");
_Static_assert(APP_COOLDOWN_2_MS < APP_COOLDOWN_3_MS, "cooldowns lengthen");
#ifndef GEIGERNODE_PROTOCOL_NATIVE
_Static_assert(TWI_REG_FW_VERSION_VALUE == 4U, "firmware version is 4");
_Static_assert(TWI_REG_HV_STATUS == 0x0BU, "HV status is register 0x0B");
#endif

#define APP_DELAY    0U
#define APP_SETTLE   1U
#define APP_RUN      2U
#define APP_FAULT    3U
#define APP_IDLE     4U
#define APP_COOLDOWN 5U

static uint16_t cps_buckets[APP_STATIC_MAX];
#ifdef GEIGERNODE_PROTOCOL_NATIVE
static uint8_t cps_extra[APP_STATIC_MAX];
#endif
static uint16_t cps_pos;
static uint16_t cps_len;
static uint16_t window_s = 300U;
static uint32_t stat_sum;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
static uint32_t stat_elapsed_ms;
static uint32_t run_elapsed_ms;
static uint8_t run_elapsed_overflow;
#endif
static uint32_t dyn_sum;
static uint8_t dyn_len;

#ifdef GEIGERNODE_PROTOCOL_NATIVE
static uint8_t snap_valid;
static uint8_t snap_full;
static uint8_t snap_overflow;
static uint8_t snap_app;
static uint8_t snap_attempts;
static uint8_t run_has_sample;
static uint32_t snap_seq;
static uint16_t peak_span = GN_PEAK_SPAN_DEFAULT_S;
static uint16_t peak_slice_len;
static uint32_t peak_slice_sum;
static uint32_t peak_slice_ms;
static uint8_t peak_win_valid;
static uint16_t peak_win_end;
static uint32_t peak_win_count;
static uint32_t peak_win_ms;
static uint8_t peak_latch_valid;
static uint32_t peak_latch_count;
static uint32_t peak_latch_ms;
static uint8_t peak_latch_dirty;
static uint8_t peak_need_rescan;
#endif

volatile uint32_t system_ms;

ISR(TIMER2_COMPA_vect)
{
	system_ms++;
}

static void timer2_start_1ms(void)
{
	OCR2A = 124;
	TCCR2A = _BV(WGM21);
	TCCR2B = _BV(CS22);
	TIMSK2 = _BV(OCIE2A);
}

static uint32_t time_ms(void)
{
	uint32_t now;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		now = system_ms;
	}
	return now;
}

static void led_fault(uint32_t now_ms, uint32_t *last_ms, uint8_t *step)
{
	/*
	 * Four short on/off pairs at 100 ms, then 700 ms dark.
	 * Steps 0,2,4,6 are on; 1,3,5,7,8 are off.
	 */
	uint16_t dwell = (*step == 8U) ? 700U : 100U;
	uint8_t next;

	if ((now_ms - *last_ms) < dwell) {
		return;
	}
	*last_ms = now_ms;
	next = (uint8_t)((*step + 1U) % 9U);
	*step = next;
	gpio_led_set((uint8_t)((next < 8U) && ((next & 1U) == 0U)));
}

static void led_cooldown(uint32_t now_ms, uint32_t *last_ms)
{
	uint32_t phase = now_ms - *last_ms;

	if (phase >= APP_COOLDOWN_BLINK_MS) {
		*last_ms = now_ms;
		gpio_led_set(1);
		return;
	}
	gpio_led_set(phase < APP_COOLDOWN_ON_MS);
}

static uint32_t cooldown_ms_for(uint8_t attempts)
{
	if (attempts <= 1U) {
		return APP_COOLDOWN_1_MS;
	}
	if (attempts == 2U) {
		return APP_COOLDOWN_2_MS;
	}
	return APP_COOLDOWN_3_MS;
}

static uint8_t hv_in_band(void)
{
	uint16_t avg;
	uint16_t target;
	uint16_t error;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		avg = adc_avg;
		target = target_adc;
	}
	if (avg > target) {
		error = (uint16_t)(avg - target);
	} else {
		error = (uint16_t)(target - avg);
	}
	return error <= HV_SETTLE_COUNTS;
}

static void windows_clear(void)
{
	uint16_t i;

	for (i = 0; i < APP_STATIC_MAX; i++) {
		cps_buckets[i] = 0;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
		cps_extra[i] = 0;
#endif
	}
	cps_pos = 0;
	cps_len = 0;
	stat_sum = 0;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	stat_elapsed_ms = 0;
	run_elapsed_ms = 0;
	run_elapsed_overflow = 0;
	peak_slice_len = 0;
	peak_slice_sum = 0;
	peak_slice_ms = 0;
	peak_win_valid = 0;
	peak_win_count = 0;
	peak_win_ms = 0;
	peak_need_rescan = 0;
#endif
	dyn_sum = 0;
	dyn_len = 0;
}

#ifdef GEIGERNODE_PROTOCOL_NATIVE
/*
 * Shrink drops the oldest seconds from the published sum. Lengthen
 * pulls older seconds still sitting in the history ring. WINDOW_FULL
 * follows cps_len >= window_s on the next snapshot.
 */
static void window_apply(uint16_t next)
{
	uint16_t old_stat;
	uint16_t new_stat;
	uint16_t idx;
	uint16_t n;
	uint32_t ms;

	if (next < 1U) {
		next = 1U;
	}
	if (next > APP_STATIC_MAX) {
		next = APP_STATIC_MAX;
	}
	if (next == window_s) {
		return;
	}
	old_stat = cps_len < window_s ? cps_len : window_s;
	new_stat = cps_len < next ? cps_len : next;
	if (new_stat > old_stat) {
		n = (uint16_t)(new_stat - old_stat);
		idx = (uint16_t)((cps_pos + APP_STATIC_MAX - new_stat) %
				 APP_STATIC_MAX);
		while (n > 0U) {
			ms = (uint32_t)APP_PUBLISH_MS + cps_extra[idx];
			stat_sum += cps_buckets[idx];
			stat_elapsed_ms += ms;
			idx = (uint16_t)((idx + 1U) % APP_STATIC_MAX);
			n--;
		}
	} else if (old_stat > new_stat) {
		n = (uint16_t)(old_stat - new_stat);
		idx = (uint16_t)((cps_pos + APP_STATIC_MAX - old_stat) %
				 APP_STATIC_MAX);
		while (n > 0U) {
			ms = (uint32_t)APP_PUBLISH_MS + cps_extra[idx];
			stat_sum -= cps_buckets[idx];
			stat_elapsed_ms -= ms;
			idx = (uint16_t)((idx + 1U) % APP_STATIC_MAX);
			n--;
		}
	}
	window_s = next;
	peak_need_rescan = 1;
}

static uint16_t peak_bucket_ms_at(uint16_t idx)
{
	return (uint16_t)(APP_PUBLISH_MS + cps_extra[idx]);
}

static int8_t peak_rate_cmp(uint32_t ca, uint32_t ma, uint32_t cb, uint32_t mb)
{
	uint64_t left;
	uint64_t right;

	if (ma == 0UL && mb == 0UL) {
		return 0;
	}
	if (ma == 0UL) {
		return -1;
	}
	if (mb == 0UL) {
		return 1;
	}
	left = (uint64_t)ca * (uint64_t)mb;
	right = (uint64_t)cb * (uint64_t)ma;
	if (left > right) {
		return 1;
	}
	if (left < right) {
		return -1;
	}
	return 0;
}

static uint16_t peak_age(uint16_t idx)
{
	return (uint16_t)((cps_pos + APP_STATIC_MAX - 1U - idx) % APP_STATIC_MAX);
}

static uint8_t peak_slice_in_window(uint16_t end_idx)
{
	uint16_t window_len = cps_len < window_s ? cps_len : window_s;

	if (window_len < peak_span) {
		return 0;
	}
	return (uint8_t)(peak_age(end_idx) + peak_span <= window_len);
}

static void peak_rescan_window(void)
{
	uint16_t window_len = cps_len < window_s ? cps_len : window_s;
	uint16_t idx;
	uint16_t n;
	uint16_t end;
	uint32_t sum;
	uint32_t ms;

	peak_win_valid = 0;
	peak_win_count = 0;
	peak_win_ms = 0;
	peak_need_rescan = 0;
	if (window_len < peak_span) {
		return;
	}
	idx = (uint16_t)((cps_pos + APP_STATIC_MAX - window_len) % APP_STATIC_MAX);
	sum = 0;
	ms = 0;
	for (n = 0; n < peak_span; n++) {
		sum += cps_buckets[idx];
		ms += peak_bucket_ms_at(idx);
		idx = (uint16_t)((idx + 1U) % APP_STATIC_MAX);
	}
	end = (uint16_t)((idx + APP_STATIC_MAX - 1U) % APP_STATIC_MAX);
	peak_win_valid = 1;
	peak_win_end = end;
	peak_win_count = sum;
	peak_win_ms = ms;
	n = (uint16_t)(window_len - peak_span);
	while (n > 0U) {
		uint16_t drop = (uint16_t)((end + APP_STATIC_MAX + 1U - peak_span) %
					   APP_STATIC_MAX);
		uint16_t add = (uint16_t)((end + 1U) % APP_STATIC_MAX);

		sum -= cps_buckets[drop];
		ms -= peak_bucket_ms_at(drop);
		sum += cps_buckets[add];
		ms += peak_bucket_ms_at(add);
		end = add;
		if (peak_rate_cmp(sum, ms, peak_win_count, peak_win_ms) >= 0) {
			peak_win_end = end;
			peak_win_count = sum;
			peak_win_ms = ms;
		}
		n--;
	}
}

static void peak_rebuild_newest_slice(void)
{
	uint16_t n;
	uint16_t idx;

	peak_slice_len = 0;
	peak_slice_sum = 0;
	peak_slice_ms = 0;
	if (cps_len == 0U || peak_span == 0U) {
		return;
	}
	n = cps_len < peak_span ? cps_len : peak_span;
	idx = (uint16_t)((cps_pos + APP_STATIC_MAX - n) % APP_STATIC_MAX);
	while (peak_slice_len < n) {
		peak_slice_sum += cps_buckets[idx];
		peak_slice_ms += peak_bucket_ms_at(idx);
		peak_slice_len++;
		idx = (uint16_t)((idx + 1U) % APP_STATIC_MAX);
	}
}

static void peak_span_apply(uint16_t next)
{
	if (next < GN_PEAK_SPAN_MIN_S) {
		next = GN_PEAK_SPAN_MIN_S;
	}
	if (next > GN_PEAK_SPAN_MAX_S) {
		next = GN_PEAK_SPAN_MAX_S;
	}
	if (next == peak_span) {
		return;
	}
	peak_span = next;
	peak_win_valid = 0;
	peak_win_count = 0;
	peak_win_ms = 0;
	peak_latch_valid = 0;
	peak_latch_count = 0;
	peak_latch_ms = 0;
	peak_latch_dirty = 0;
	peak_rebuild_newest_slice();
	peak_need_rescan = 1;
	twi_slave_set_native_peaks(0, 0, 0, 0);
}

static void peak_on_bucket(uint16_t delta, uint16_t bucket_ms)
{
	uint16_t newest;
	uint16_t drop;

	newest = (uint16_t)((cps_pos + APP_STATIC_MAX - 1U) % APP_STATIC_MAX);
	if (peak_slice_len < peak_span) {
		peak_slice_sum += delta;
		peak_slice_ms += bucket_ms;
		peak_slice_len++;
	} else {
		drop = (uint16_t)((cps_pos + APP_STATIC_MAX - peak_span - 1U) %
				  APP_STATIC_MAX);
		peak_slice_sum += delta;
		peak_slice_ms += bucket_ms;
		peak_slice_sum -= cps_buckets[drop];
		peak_slice_ms -= peak_bucket_ms_at(drop);
	}
	if (peak_slice_len < peak_span) {
		return;
	}
	if (!peak_win_valid ||
	    peak_rate_cmp(peak_slice_sum, peak_slice_ms, peak_win_count,
			  peak_win_ms) >= 0) {
		peak_win_valid = 1;
		peak_win_end = newest;
		peak_win_count = peak_slice_sum;
		peak_win_ms = peak_slice_ms;
	} else if (!peak_slice_in_window(peak_win_end)) {
		peak_need_rescan = 1;
	}
	if (!peak_latch_valid ||
	    peak_rate_cmp(peak_slice_sum, peak_slice_ms, peak_latch_count,
			  peak_latch_ms) >= 0) {
		peak_latch_valid = 1;
		peak_latch_count = peak_slice_sum;
		peak_latch_ms = peak_slice_ms;
		peak_latch_dirty = 1;
	}
}

static void peak_rates(uint32_t *win_cpm, uint32_t *latch_cpm,
		       uint32_t *win_dose, uint32_t *latch_dose)
{
	uint8_t ov = 0;
	uint32_t cal = gn_config_cal_q16();

	*win_cpm = 0;
	*latch_cpm = 0;
	*win_dose = 0;
	*latch_dose = 0;
	if (peak_need_rescan ||
	    (peak_win_valid && !peak_slice_in_window(peak_win_end))) {
		peak_rescan_window();
	}
	if (peak_win_valid) {
		*win_cpm = radiation_cpm_x10(peak_win_count, peak_win_ms, &ov);
		*win_dose = radiation_dose_nsvh(peak_win_count, peak_win_ms, cal,
					       &ov);
	}
	if (peak_latch_valid) {
		*latch_cpm = radiation_cpm_x10(peak_latch_count, peak_latch_ms,
					      &ov);
		*latch_dose = radiation_dose_nsvh(peak_latch_count, peak_latch_ms,
						  cal, &ov);
	}
}

static void peak_on_latch_consume(void)
{
	uint32_t win_cpm;
	uint32_t latch_cpm;
	uint32_t win_dose;
	uint32_t latch_dose;

	if (peak_latch_dirty) {
		peak_latch_dirty = 0;
	} else {
		peak_latch_valid = 0;
		peak_latch_count = 0;
		peak_latch_ms = 0;
	}
	peak_rates(&win_cpm, &latch_cpm, &win_dose, &latch_dose);
	twi_slave_set_native_peaks(win_cpm, latch_cpm, win_dose, latch_dose);
}
#endif

static uint32_t sum_recent(uint16_t count)
{
	uint32_t sum = 0;
	uint16_t i;
	uint16_t idx = cps_pos;

	if (count > dyn_len) {
		count = dyn_len;
	}
	for (i = 0; i < count; i++) {
		if (idx == 0U) {
			idx = APP_STATIC_MAX;
		}
		idx--;
		sum += cps_buckets[idx];
	}
	return sum;
}

static void windows_push(uint16_t delta, uint32_t dt)
{
	uint32_t recent;
	uint32_t older;
	uint16_t older_len;
	uint32_t recent_scaled;
	uint32_t older_scaled;
	uint32_t busier;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	uint16_t extra;
	uint16_t bucket_ms;

	if (dt <= APP_PUBLISH_MS) {
		extra = 0;
		bucket_ms = (uint16_t)APP_PUBLISH_MS;
	} else if ((dt - APP_PUBLISH_MS) > 255UL) {
		extra = 255U;
		bucket_ms = (uint16_t)(APP_PUBLISH_MS + 255U);
	} else {
		extra = (uint16_t)(dt - APP_PUBLISH_MS);
		bucket_ms = (uint16_t)(APP_PUBLISH_MS + extra);
	}
	/* Run time keeps counting after the published window is full. */
	if (run_elapsed_ms > (0xFFFFFFFFUL - bucket_ms)) {
		run_elapsed_ms = 0xFFFFFFFFUL;
		run_elapsed_overflow = 1U;
	} else {
		run_elapsed_ms += bucket_ms;
	}
#else
	(void)dt;
#endif

	/*
	 * The history ring holds APP_STATIC_MAX seconds. The published
	 * static window is the newest window_s of those. Overwriting the
	 * oldest history bucket removes it from the static sum only when
	 * the published window covers the whole ring. A shorter window
	 * drops the bucket that just slid out of the newest window_s.
	 */
	if (cps_len == APP_STATIC_MAX && window_s >= cps_len) {
		stat_sum -= cps_buckets[cps_pos];
#ifdef GEIGERNODE_PROTOCOL_NATIVE
		stat_elapsed_ms -= (uint32_t)APP_PUBLISH_MS + cps_extra[cps_pos];
#endif
	}
	if (dyn_len == APP_DYNAMIC_MAX) {
		uint16_t drop = (uint16_t)((cps_pos + APP_STATIC_MAX -
					    APP_DYNAMIC_MAX) %
					   APP_STATIC_MAX);

		dyn_sum -= cps_buckets[drop];
	}
	cps_buckets[cps_pos] = delta;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	cps_extra[cps_pos] = (uint8_t)extra;
#endif
	stat_sum += delta;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	stat_elapsed_ms += bucket_ms;
#endif
	dyn_sum += delta;
	cps_pos = (uint16_t)((cps_pos + 1U) % APP_STATIC_MAX);
	if (cps_len < APP_STATIC_MAX) {
		cps_len++;
	}
	if (dyn_len < APP_DYNAMIC_MAX) {
		dyn_len++;
	}
	if (cps_len > window_s) {
		uint16_t drop = (uint16_t)((cps_pos + APP_STATIC_MAX - window_s -
					    1U) %
					   APP_STATIC_MAX);

		stat_sum -= cps_buckets[drop];
#ifdef GEIGERNODE_PROTOCOL_NATIVE
		stat_elapsed_ms -= (uint32_t)APP_PUBLISH_MS + cps_extra[drop];
#endif
	}
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	peak_on_bucket(delta, bucket_ms);
#endif
	if (dyn_len <= APP_DYNAMIC_RECENT) {
		return;
	}

	/*
	 * Shrink to the last 10 s when that slice and the older part of
	 * the window differ by more than a factor of two. A busier side
	 * below 4 counts is background noise, not a new rate.
	 */
	recent = sum_recent(APP_DYNAMIC_RECENT);
	if (recent > dyn_sum) {
		return;
	}
	older = dyn_sum - recent;
	older_len = (uint16_t)(dyn_len - APP_DYNAMIC_RECENT);
	busier = recent > older ? recent : older;
	if (busier < APP_SHRINK_COUNTS) {
		return;
	}
	recent_scaled = recent * older_len;
	older_scaled = older * APP_DYNAMIC_RECENT;
	if (recent_scaled > older_scaled * 2UL ||
	    older_scaled > recent_scaled * 2UL) {
		dyn_len = APP_DYNAMIC_RECENT;
		dyn_sum = recent;
	}
}

#ifdef GEIGERNODE_PROTOCOL_NATIVE
static uint16_t native_status(void)
{
	uint16_t s = 0;
	uint8_t attempts = snap_attempts;

	if (snap_valid) {
		s |= GN_STATUS_VALID;
	}
	if (snap_full) {
		s |= GN_STATUS_WINDOW_FULL;
	}
	if (snap_app == APP_DELAY || snap_app == APP_SETTLE ||
	    snap_app == APP_RUN || snap_app == APP_COOLDOWN) {
		s |= GN_STATUS_HV_FOLLOWING;
	}
	if (snap_app == APP_SETTLE) {
		s |= GN_STATUS_HV_SETTLING;
	}
	if (snap_app == APP_COOLDOWN) {
		s |= GN_STATUS_HV_COOLDOWN;
	}
	if (snap_app == APP_FAULT) {
		s |= GN_STATUS_HV_FAULT;
	}
	if (snap_overflow) {
		s |= GN_STATUS_OVERFLOW;
	}
	if (attempts > 15U) {
		attempts = 15U;
	}
	return (uint16_t)(s | ((uint16_t)attempts << 8));
}

static void native_touch_status(uint8_t app, uint8_t attempts)
{
	snap_app = app;
	snap_attempts = attempts;
	snap_valid = (uint8_t)((app == APP_RUN) && run_has_sample);
	twi_slave_set_native_status(native_status());
}

static void native_publish_rates(uint8_t app, uint8_t attempts)
{
	uint8_t ov_cpm = 0;
	uint8_t ov_dose = 0;
	uint32_t cpm;
	uint32_t dose;
	uint32_t window_elapsed = stat_elapsed_ms;
	uint32_t peak_win_cpm;
	uint32_t peak_latch_cpm;
	uint32_t peak_win_dose;
	uint32_t peak_latch_dose;
	struct gm_histogram histogram;

	cpm = radiation_cpm_x10(stat_sum, window_elapsed, &ov_cpm);
	dose = radiation_dose_nsvh(stat_sum, window_elapsed, gn_config_cal_q16(),
				   &ov_dose);
	gm_counter_histogram(&histogram);
	peak_rates(&peak_win_cpm, &peak_latch_cpm, &peak_win_dose,
		   &peak_latch_dose);
	if (snap_seq < 0xFFFFFFFFUL) {
		snap_seq++;
	} else {
		ov_cpm = 1;
	}
	run_has_sample = 1;
	snap_valid = 1;
	snap_full = (uint8_t)(cps_len >= window_s);
	snap_overflow = (uint8_t)(ov_cpm | ov_dose | run_elapsed_overflow);
	snap_app = app;
	snap_attempts = attempts;
	twi_slave_publish_native(dose, cpm, native_status(), snap_seq,
				 gn_config_cal_q16(), stat_sum, run_elapsed_ms,
				 &histogram, peak_win_cpm, peak_latch_cpm,
				 peak_win_dose, peak_latch_dose);
	peak_latch_dirty = 0;
}
#endif

static void publish_rates(uint8_t app, uint8_t attempts)
{
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	native_publish_rates(app, attempts);
#else
	uint16_t sens = radsens_sensitivity();
	uint32_t dynamic_x10;
	uint32_t static_x10;

	(void)app;
	(void)attempts;
	dynamic_x10 = radiation_counts_to_urh_x10(dyn_sum,
		(uint32_t)dyn_len * APP_PUBLISH_MS, sens);
	static_x10 = radiation_counts_to_urh_x10(stat_sum,
		(uint32_t)cps_len * APP_PUBLISH_MS, sens);
	radsens_publish_radiation(dynamic_x10, static_x10);
#endif
}

static void enter_fault(uint8_t attempts, uint32_t now_ms, uint32_t *last_ms,
			uint8_t *fault_step, uint8_t *app)
{
	hv_gm_quiesce();
	gm_counter_close_window();
	radsens_on_fault();
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	native_touch_status(APP_FAULT, attempts);
#else
	radsens_publish_radiation(0, 0);
	radsens_set_hv_status(RADSENS_HV_GIVEN_UP, attempts);
#endif
	*fault_step = 0;
	*last_ms = now_ms;
	gpio_led_set(1);
	*app = APP_FAULT;
}

static void enter_idle(uint8_t *attempts, uint8_t *app)
{
	hv_gm_quiesce();
	gm_counter_close_window();
	*attempts = 0;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	native_touch_status(APP_IDLE, 0);
#else
	radsens_publish_radiation(0, 0);
	radsens_set_hv_status(RADSENS_HV_FOLLOWING, 0);
#endif
	gpio_led_set(0);
	*app = APP_IDLE;
}

static void enter_cooldown(uint8_t attempts, uint32_t now_ms, uint32_t *phase_ms,
			   uint32_t *cooldown_ms, uint32_t *last_ms, uint8_t *app)
{
	hv_gm_quiesce();
	gm_counter_close_window();
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	native_touch_status(APP_COOLDOWN, attempts);
#else
	radsens_publish_radiation(0, 0);
	radsens_set_hv_status(RADSENS_HV_COOLDOWN, attempts);
#endif
	*phase_ms = now_ms;
	*cooldown_ms = cooldown_ms_for(attempts);
	*last_ms = now_ms;
	gpio_led_set(1);
	*app = APP_COOLDOWN;
}

static void fail_hv(uint16_t status, uint8_t *attempts, uint32_t now_ms,
		    uint32_t *phase_ms, uint32_t *cooldown_ms, uint32_t *last_ms,
		    uint8_t *fault_step, uint8_t *app)
{
	/*
	 * A cleared budget means this run is the first start of a new
	 * series. Count it before deciding to wait or give up.
	 */
	if (*attempts == 0U) {
		*attempts = 1U;
	}
	if (status == HV_TEST_FAULT || *attempts >= APP_ATTEMPTS) {
		enter_fault(*attempts, now_ms, last_ms, fault_step, app);
		return;
	}
	enter_cooldown(*attempts, now_ms, phase_ms, cooldown_ms, last_ms, app);
}

static uint8_t enter_settle(uint32_t now_ms, uint32_t *phase_ms, uint32_t *settle_ok_ms,
			    uint8_t *attempts, uint8_t *app)
{
	if (!hv_gm_begin((uint16_t)now_ms)) {
		return 0;
	}
	if (*attempts < 255U) {
		(*attempts)++;
	}
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	native_touch_status(APP_SETTLE, *attempts);
#else
	radsens_set_hv_status(RADSENS_HV_FOLLOWING, *attempts);
#endif
	*phase_ms = now_ms;
	*settle_ok_ms = 0;
	gpio_led_set(0);
	*app = APP_SETTLE;
	return 1;
}

static void enter_run(uint32_t now_ms, uint32_t *last_publish_ms,
		      uint32_t *counted_window, uint32_t *bucket_window,
		      uint8_t attempts, uint8_t *app)
{
	gm_counter_open_window();
	*counted_window = 0;
	*bucket_window = 0;
	*last_publish_ms = now_ms;
	windows_clear();
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	run_has_sample = 0;
	native_touch_status(APP_RUN, attempts);
#else
	(void)attempts;
	twi_slave_clear_pulses();
	radsens_publish_radiation(0, 0);
#endif
	*app = APP_RUN;
}

int main(void)
{
	uint32_t last_toggle_ms = 0;
	uint32_t pulse_on_until = 0;
	uint32_t phase_ms = 0;
	uint32_t settle_ok_ms = 0;
	uint32_t cooldown_ms = 0;
	uint32_t run_ok_ms = 0;
	uint32_t last_publish_ms = 0;
	uint32_t counted_window = 0;
	uint32_t bucket_window = 0;
	uint8_t fault_step = 0;
	uint8_t attempts = 0;
	uint8_t app = APP_DELAY;

	MCUSR = 0;
	windows_clear();
	/* PB1 low and OC1A disconnected before any other peripheral starts. */
	gpio_init();
	adc_init();
	timer2_start_1ms();
	gm_counter_init();
	twi_slave_init();
	radsens_init();
#ifdef GEIGERNODE_PROTOCOL_NATIVE
	window_apply(gn_config_window_s());
	peak_span_apply(gn_config_peak_span_s());
	native_touch_status(APP_DELAY, 0);
#endif
	sei();

	for (;;) {
		uint32_t now_ms = time_ms();
		uint8_t hv_cmd;

		radsens_poll();
#ifdef GEIGERNODE_PROTOCOL_NATIVE
		window_apply(gn_config_window_s());
		peak_span_apply(gn_config_peak_span_s());
		if (twi_slave_take_peak_latch_consume()) {
			peak_on_latch_consume();
		}
#endif
		hv_cmd = radsens_hv_command();

		if (app == APP_DELAY) {
			gpio_led_set(radsens_led_enabled());
			if (now_ms >= APP_PRE_HV_MS) {
				if (hv_cmd) {
					if (!enter_settle(now_ms, &phase_ms, &settle_ok_ms,
							  &attempts, &app)) {
						enter_fault(attempts, now_ms, &last_toggle_ms,
							    &fault_step, &app);
					}
				} else {
					gpio_led_set(0);
					app = APP_IDLE;
				}
			}
		} else if (app == APP_IDLE) {
			gpio_led_set(0);
			if (hv_cmd) {
				if (!enter_settle(now_ms, &phase_ms, &settle_ok_ms,
						  &attempts, &app)) {
					enter_fault(attempts, now_ms, &last_toggle_ms,
						    &fault_step, &app);
				}
			}
		} else if (app == APP_FAULT) {
			led_fault(now_ms, &last_toggle_ms, &fault_step);
			if (hv_cmd) {
				attempts = 0;
				if (!enter_settle(now_ms, &phase_ms, &settle_ok_ms,
						  &attempts, &app)) {
					enter_fault(attempts, now_ms, &last_toggle_ms,
						    &fault_step, &app);
				}
			}
		} else if (app == APP_COOLDOWN) {
			led_cooldown(now_ms, &last_toggle_ms);
			if (!hv_cmd) {
				enter_idle(&attempts, &app);
			} else if ((now_ms - phase_ms) >= cooldown_ms) {
				if (!enter_settle(now_ms, &phase_ms, &settle_ok_ms,
						  &attempts, &app)) {
					enter_fault(attempts, now_ms, &last_toggle_ms,
						    &fault_step, &app);
				}
			}
		} else if (!hv_cmd) {
			enter_idle(&attempts, &app);
		} else {
			hv_gm_poll((uint16_t)now_ms);
			if (hv_is_fault()) {
				fail_hv(hv_latched_status(), &attempts, now_ms, &phase_ms,
					&cooldown_ms, &last_toggle_ms, &fault_step, &app);
			} else if (app == APP_SETTLE) {
				if ((now_ms - phase_ms) >= HV_SETTLE_TIMEOUT_MS) {
					fail_hv(hv_latched_status(), &attempts, now_ms,
						&phase_ms, &cooldown_ms, &last_toggle_ms,
						&fault_step, &app);
				} else if (hv_is_regulating() && hv_in_band()) {
					if (settle_ok_ms == 0UL) {
						settle_ok_ms = now_ms;
					} else if ((now_ms - settle_ok_ms) >= HV_SETTLE_HOLD_MS) {
						run_ok_ms = now_ms;
						enter_run(now_ms, &last_publish_ms,
							  &counted_window, &bucket_window,
							  attempts, &app);
					}
				} else {
					settle_ok_ms = 0;
				}
			} else {
				uint32_t window = gm_counter_window();
				uint32_t add = window - counted_window;

				if (attempts != 0U &&
				    (now_ms - run_ok_ms) >= APP_BUDGET_CLEAR_MS) {
					attempts = 0;
#ifdef GEIGERNODE_PROTOCOL_NATIVE
					native_touch_status(APP_RUN, 0);
#else
					radsens_set_hv_status(RADSENS_HV_FOLLOWING, 0);
#endif
				}
				counted_window = window;
				if (add > 65535UL) {
					add = 65535UL;
				}
#ifndef GEIGERNODE_PROTOCOL_NATIVE
				twi_slave_count_pulses((uint16_t)add);
#endif
				if ((now_ms - last_publish_ms) >= APP_PUBLISH_MS) {
					uint32_t delta = window - bucket_window;
					uint32_t dt = now_ms - last_publish_ms;

					last_publish_ms = now_ms;
					bucket_window = window;
					if (delta > 65535UL) {
						delta = 65535UL;
					}
					windows_push((uint16_t)delta, dt);
					publish_rates(APP_RUN, attempts);
				}
				if (radsens_led_enabled() && add > 0UL) {
					pulse_on_until = now_ms + GM_PULSE_LED_MS;
				}
				if (radsens_led_enabled()) {
					gpio_led_set((int32_t)(pulse_on_until - now_ms) > 0);
				} else {
					gpio_led_set(0);
				}
			}
		}
	}
}
