#include <avr/eeprom.h>
#include <stddef.h>
#include <stdint.h>

#include "gm_counter.h"
#include "gm_test.h"
#include "hv.h"

#if HV_TARGET_V != 380 || HV_SWITCH_FREQ_HZ != 2400
#error "SI-29BG bring-up is 380 V at 2400 Hz"
#endif
#if HV_MAX_PULSE_US != 100 || HV_ADC_HARD_ABORT != 932
#error "HV hard limits are 100 us and 932 counts"
#endif

_Static_assert(sizeof(struct gm_test_result) == 106U, "GM record is 106 bytes");
_Static_assert(offsetof(struct gm_test_result, close_intervals_us) == 42U,
	       "close-interval log follows the histogram");
_Static_assert(offsetof(struct gm_test_result, close_interval_count) == 74U,
	       "close-interval count follows the 16 stored gaps");
_Static_assert(offsetof(struct gm_test_result, state) == 102U, "state is byte 102");
_Static_assert(offsetof(struct gm_test_result, checksum) == 104U, "checksum is byte 104");
_Static_assert(offsetof(struct gm_test_result, min_interval_us) == 20U,
	       "min interval is the first diagnostic field");
_Static_assert(GM_BLANKING_US == 0U, "software dead time stays off");
_Static_assert(GM_CLOSE_LOG == 16U, "the log holds 16 close gaps");
_Static_assert(GM_CLOSE_LIMIT_US == 500U, "the log keeps gaps below 500 us");
_Static_assert(GM_TEST_EEPROM_ADDR > 178U, "GM record sits past a frequency-series image");
_Static_assert(((5000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 12ULL,
	       "5 V is 12 counts");
_Static_assert(((380000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 885ULL,
	       "380 V is 885 counts");
_Static_assert(885U + GM_HV_SETTLE_COUNTS < HV_ADC_HARD_ABORT,
	       "settle band stays under the 400 V abort");
_Static_assert(GM_TUBE_SI29BG_HV_V == HV_TARGET_V, "tube profile matches the HV target");
_Static_assert(GM_TUBE_SI29BG_SENSITIVITY_UNUSED == 0U, "sensitivity is not a scale factor");
_Static_assert(GM_MEASURE_MS == 600000UL, "background window is 10 minutes");
_Static_assert(GM_MEASURE_BUCKETS == 600UL, "one bucket per second");

enum {
	GM_IDLE = 0,
	GM_WAIT,
	GM_DELAY,
	GM_SETTLE,
	GM_MEASURE,
	GM_COMPLETE,
	GM_FAULT
};

static struct gm_test_result record;
static uint8_t phase;
static uint8_t record_closed;
static uint8_t hv_seen;
static uint8_t measure_started;
static uint8_t band_valid;
static uint8_t cps_saturated;
static uint16_t buckets;
static uint16_t min_cps;
static uint16_t max_cps;
static uint16_t min_adc;
static uint16_t max_adc;
static uint16_t min_hv;
static uint16_t max_hv;
static uint16_t min_pulse;
static uint16_t max_pulse;
static uint16_t last_adc;
static uint16_t last_hv;
static uint16_t last_pulse;
static uint32_t settle_begin_ms;
static uint32_t band_since_ms;
static uint32_t measure_begin_ms;
static uint32_t bucket_base;
static uint32_t cap_base_ms;

static void gm_store(void)
{
	record.checksum = gm_test_checksum(&record);
	eeprom_update_block(&record, (void *)GM_TEST_EEPROM_ADDR, sizeof record);
}

static uint8_t gm_is_blank(void)
{
	return record.magic == 0xFFFFU && record.version == 0xFFFFU &&
	       record.state == 0xFFFFU;
}

static uint8_t gm_is_valid(void)
{
	return record.magic == GM_TEST_MAGIC && record.version == GM_TEST_VERSION &&
	       record.checksum == gm_test_checksum(&record);
}

static void gm_note_hv(uint8_t track_range)
{
	uint16_t avg = adc_avg;
	uint16_t volts = (uint16_t)(estimated_hv_mv / 1000UL);
	uint16_t width = pulse_width_us;

	hv_seen = 1;
	last_adc = avg;
	last_hv = volts;
	last_pulse = width;
	if (!track_range) {
		return;
	}
	if (avg < min_adc) {
		min_adc = avg;
	}
	if (avg > max_adc) {
		max_adc = avg;
	}
	if (volts < min_hv) {
		min_hv = volts;
	}
	if (volts > max_hv) {
		max_hv = volts;
	}
	if (width > max_pulse) {
		max_pulse = width;
	}
	if (width > 0 && width < min_pulse) {
		min_pulse = width;
	}
}

static void gm_capture_fault_sample(void)
{
	uint16_t latched = hv_latched_pulse_us();

	uint16_t avg = adc_avg;
	uint16_t raw = adc_raw;

	hv_seen = 1;
	last_adc = avg > raw ? avg : raw;
	last_hv = (uint16_t)(estimated_hv_mv / 1000UL);
	last_pulse = latched;
	if (latched > max_pulse) {
		max_pulse = latched;
	}
	if (measure_started) {
		if (last_adc < min_adc) {
			min_adc = last_adc;
		}
		if (last_adc > max_adc) {
			max_adc = last_adc;
		}
		if (last_hv < min_hv) {
			min_hv = last_hv;
		}
		if (last_hv > max_hv) {
			max_hv = last_hv;
		}
	}
}

static void gm_account_cps(uint32_t cps)
{
	if (cps > 65535UL) {
		cps = 65535UL;
		cps_saturated = 1;
	}
	if ((uint16_t)cps < min_cps) {
		min_cps = (uint16_t)cps;
	}
	if ((uint16_t)cps > max_cps) {
		max_cps = (uint16_t)cps;
	}
}

static void gm_raise_max_cps(uint32_t cps)
{
	if (cps > 65535UL) {
		cps = 65535UL;
		cps_saturated = 1;
	}
	if ((uint16_t)cps > max_cps) {
		max_cps = (uint16_t)cps;
	}
}

static uint16_t gm_status_from_hv(void)
{
	switch (hv_latched_status()) {
	case HV_TEST_OVERVOLTAGE:
		return GM_TEST_OVERVOLTAGE;
	case HV_TEST_TIMEOUT:
		return GM_TEST_HV_TIMEOUT;
	default:
		return GM_TEST_HV_FAULT;
	}
}

static uint32_t gm_intervals_below_1ms(const struct gm_counter_snapshot *snap)
{
	return (uint32_t)snap->bin_lt_50us + snap->bin_50_100us +
	       snap->bin_100_200us + snap->bin_200_500us + snap->bin_500us_1ms;
}

static uint16_t gm_measurement_status(const struct gm_counter_snapshot *snap)
{
	uint32_t below_1ms;

	if (snap->window_count == 0) {
		return GM_TEST_NO_PULSES;
	}
	below_1ms = gm_intervals_below_1ms(snap);
	if (cps_saturated || below_1ms >= GM_SUSPECT_LT_1MS) {
		return GM_TEST_EXCESSIVE_FAST_PULSES;
	}
	if (snap->window_count >= GM_SUSPECT_MIN_PULSES &&
	    below_1ms + snap->bin_1_2ms > snap->window_count / 2UL) {
		return GM_TEST_EXCESSIVE_FAST_PULSES;
	}
	return GM_TEST_OK;
}

static uint8_t hv_in_settle_band(void)
{
	uint16_t avg = adc_avg;
	uint16_t target = target_adc;
	uint16_t low;
	uint16_t high;

	if (regulator_state != HV_REGULATING) {
		return 0;
	}
	low = target > GM_HV_SETTLE_COUNTS ? (uint16_t)(target - GM_HV_SETTLE_COUNTS) : 0;
	high = (uint16_t)(target + GM_HV_SETTLE_COUNTS);
	return avg >= low && avg <= high;
}

static void gm_commit(uint16_t end_state, uint16_t result_status, uint8_t window_done)
{
	struct gm_counter_snapshot snap;

	if (record_closed) {
		hv_gm_stop();
		return;
	}

	gm_counter_close_window();
	gm_counter_snapshot(&snap);
	/* Disconnect OC1A and force PB1 low before the EEPROM write. */
	hv_gm_stop();
	if (window_done && snap.window_count > bucket_base) {
		gm_raise_max_cps(snap.window_count - bucket_base);
	}
	if (window_done) {
		result_status = gm_measurement_status(&snap);
	}

	record.magic = GM_TEST_MAGIC;
	record.version = GM_TEST_VERSION;
	record.target_hv_v = HV_TARGET_V;
	record.frequency_hz = HV_SWITCH_FREQ_HZ;
	record.total_pulses = snap.total_count;
	record.measurement_pulses = snap.window_count;
	/*
	 * Integer counts per minute. The raw window count stays in
	 * measurement_pulses, so a fractional CPM is not lost.
	 */
	record.measured_cpm = window_done ? (snap.window_count * 60UL) /
						    (GM_MEASURE_MS / 1000UL) :
					    GM_CPM_UNSET;
	record.min_interval_us = snap.min_interval_us;
	record.bin_lt_50us = snap.bin_lt_50us;
	record.bin_50_100us = snap.bin_50_100us;
	record.bin_100_200us = snap.bin_100_200us;
	record.bin_200_500us = snap.bin_200_500us;
	record.bin_500us_1ms = snap.bin_500us_1ms;
	record.bin_1_2ms = snap.bin_1_2ms;
	record.bin_2_5ms = snap.bin_2_5ms;
	record.bin_5_10ms = snap.bin_5_10ms;
	record.bin_ge_10ms = snap.bin_ge_10ms;
	record.close_interval_count = snap.close_interval_count;
	for (uint8_t i = 0; i < GM_CLOSE_LOG; i++) {
		record.close_intervals_us[i] = snap.close_intervals_us[i];
	}
	record.status = result_status;
	record.state = end_state;

	if (min_cps == 0xFFFFU) {
		max_cps = 0xFFFFU;
	}
	record.min_cps = min_cps;
	record.max_cps = max_cps;

	if (hv_seen && min_adc == 0xFFFFU) {
		min_adc = last_adc;
		max_adc = last_adc;
		min_hv = last_hv;
		max_hv = last_hv;
		if (last_pulse == 0) {
			min_pulse = 0xFFFFU;
			max_pulse = 0;
		} else {
			min_pulse = last_pulse;
			max_pulse = last_pulse;
		}
	}
	if (!hv_seen) {
		last_adc = 0xFFFFU;
		min_adc = 0xFFFFU;
		max_adc = 0xFFFFU;
		last_hv = 0xFFFFU;
		min_hv = 0xFFFFU;
		max_hv = 0xFFFFU;
		last_pulse = 0xFFFFU;
		min_pulse = 0xFFFFU;
		max_pulse = 0xFFFFU;
	}
	record.final_adc = last_adc;
	record.min_adc = min_adc;
	record.max_adc = max_adc;
	record.final_hv_v = last_hv;
	record.min_hv_v = min_hv;
	record.max_hv_v = max_hv;
	record.final_pulse_us = last_pulse;
	record.min_pulse_us = min_pulse;
	record.max_pulse_us = max_pulse;
	if (measure_started) {
		uint32_t at_cap = hv_cap_milliseconds() - cap_base_ms;

		record.ms_at_100us = at_cap > 65535UL ? 65535U : (uint16_t)at_cap;
	} else {
		record.ms_at_100us = 0xFFFFU;
	}

	gm_store();
	record_closed = 1;
	phase = end_state == GM_STATE_COMPLETE ? GM_COMPLETE : GM_FAULT;
}

/* Commit RUNNING without touching the phase. HV is still off here. */
static void gm_write_running(void)
{
	uint8_t *raw = (uint8_t *)&record;
	uint16_t i;

	for (i = 0; i < sizeof record; i++) {
		raw[i] = 0;
	}
	record.magic = GM_TEST_MAGIC;
	record.version = GM_TEST_VERSION;
	record.target_hv_v = HV_TARGET_V;
	record.frequency_hz = HV_SWITCH_FREQ_HZ;
	record.measured_cpm = GM_CPM_UNSET;
	record.min_cps = 0xFFFFU;
	record.max_cps = 0xFFFFU;
	record.min_interval_us = 0xFFFFFFFFUL;
	record.final_adc = 0xFFFFU;
	record.min_adc = 0xFFFFU;
	record.max_adc = 0xFFFFU;
	record.final_hv_v = 0xFFFFU;
	record.min_hv_v = 0xFFFFU;
	record.max_hv_v = 0xFFFFU;
	record.final_pulse_us = 0xFFFFU;
	record.min_pulse_us = 0xFFFFU;
	record.max_pulse_us = 0xFFFFU;
	record.ms_at_100us = 0xFFFFU;
	record.state = GM_STATE_RUNNING;
	gm_store();
}

static void gm_open_measurement(uint32_t now_ms)
{
	gm_counter_open_window();
	measure_started = 1;
	measure_begin_ms = now_ms;
	buckets = 0;
	bucket_base = 0;
	min_cps = 0xFFFFU;
	max_cps = 0;
	cps_saturated = 0;
	cap_base_ms = hv_cap_milliseconds();
	min_adc = 0xFFFFU;
	max_adc = 0;
	min_hv = 0xFFFFU;
	max_hv = 0;
	min_pulse = 0xFFFFU;
	max_pulse = 0;
	phase = GM_MEASURE;
}

static void gm_finish_hv(uint16_t result_status)
{
	if (regulator_state == HV_FAULT) {
		gm_capture_fault_sample();
	}
	gm_commit(GM_STATE_ABORTED, result_status, 0);
}

void gm_test_boot(uint8_t reset_flags)
{
	min_cps = 0xFFFFU;
	max_cps = 0xFFFFU;
	min_adc = 0xFFFFU;
	max_adc = 0xFFFFU;
	min_hv = 0xFFFFU;
	max_hv = 0xFFFFU;
	min_pulse = 0xFFFFU;
	max_pulse = 0xFFFFU;
	hv_gm_stop();
	eeprom_read_block(&record, (const void *)GM_TEST_EEPROM_ADDR, sizeof record);

	if (gm_is_blank() || !gm_is_valid()) {
		phase = GM_IDLE;
		return;
	}
	/*
	 * A programmer reset leaves ARMED alone so the board can be carried
	 * unpowered. High voltage starts only on a later power-on, when PORF
	 * is set and EXTRF is clear. RUNNING is written at the end of the
	 * 5 s delay, so removing power during that delay leaves ARMED.
	 */
	if (record.state == GM_STATE_ARMED) {
		if ((reset_flags & _BV(PORF)) && !(reset_flags & _BV(EXTRF))) {
			phase = GM_DELAY;
		} else {
			phase = GM_WAIT;
		}
		return;
	}
	if (record.state == GM_STATE_RUNNING) {
		gm_commit(GM_STATE_ABORTED, GM_TEST_ABORTED, 0);
		return;
	}
	if (record.state == GM_STATE_COMPLETE) {
		record_closed = 1;
		phase = GM_COMPLETE;
		return;
	}
	if (record.state == GM_STATE_ABORTED) {
		record_closed = 1;
		phase = GM_FAULT;
		return;
	}
	phase = GM_IDLE;
}

void gm_test_poll(uint32_t now_ms)
{
	/*
	 * The HV control timestamps are the low 16 bits. Its intervals are
	 * 20 ms and 10 s, both inside one uint16 span.
	 */
	uint16_t hv_now = (uint16_t)now_ms;

	if (phase == GM_IDLE || phase == GM_COMPLETE || phase == GM_FAULT ||
	    phase == GM_WAIT) {
		if (!hv_switch_is_off()) {
			hv_gm_stop();
		}
		return;
	}

	if (phase == GM_DELAY) {
		if (!hv_switch_is_off()) {
			gm_finish_hv(GM_TEST_HV_FAULT);
			return;
		}
		if (now_ms < GM_PRE_HV_MS) {
			return;
		}
		gm_write_running();
		if (gm_tube_nominal_hv_v() != HV_TARGET_V || !hv_gm_begin(hv_now)) {
			gm_finish_hv(GM_TEST_HV_FAULT);
			return;
		}
		settle_begin_ms = now_ms;
		band_valid = 0;
		phase = GM_SETTLE;
		return;
	}

	if (phase == GM_SETTLE) {
		hv_gm_poll(hv_now);
		if (regulator_state == HV_FAULT) {
			gm_finish_hv(gm_status_from_hv());
			return;
		}
		gm_note_hv(0);
		if ((now_ms - settle_begin_ms) >= GM_HV_SETTLE_TIMEOUT_MS) {
			gm_finish_hv(GM_TEST_HV_TIMEOUT);
			return;
		}
		if (hv_in_settle_band()) {
			if (!band_valid) {
				band_since_ms = now_ms;
				band_valid = 1;
			} else if ((now_ms - band_since_ms) >= GM_HV_SETTLE_HOLD_MS) {
				gm_open_measurement(now_ms);
			}
		} else {
			band_valid = 0;
		}
		return;
	}

	if (phase == GM_MEASURE) {
		hv_gm_poll(hv_now);
		if (regulator_state == HV_FAULT) {
			gm_finish_hv(gm_status_from_hv());
			return;
		}
		gm_note_hv(1);
		while (buckets < GM_MEASURE_BUCKETS &&
		       (now_ms - measure_begin_ms) >= (uint32_t)(buckets + 1U) * 1000UL) {
			uint32_t count = gm_counter_window();

			gm_account_cps(count - bucket_base);
			bucket_base = count;
			buckets++;
		}
		if ((now_ms - measure_begin_ms) >= GM_MEASURE_MS) {
			gm_commit(GM_STATE_COMPLETE, 0, 1);
		}
	}
}

uint8_t gm_test_indicator(void)
{
	if (phase == GM_WAIT || phase == GM_DELAY) {
		return HV_LED_SLOW;
	}
	if (phase == GM_SETTLE) {
		return HV_LED_FAST;
	}
	if (phase == GM_MEASURE || phase == GM_COMPLETE) {
		return HV_LED_ON;
	}
	if (phase == GM_FAULT) {
		return HV_LED_FAULT;
	}
	return HV_LED_OFF;
}

uint8_t gm_test_is_measuring(void)
{
	return phase == GM_MEASURE;
}
