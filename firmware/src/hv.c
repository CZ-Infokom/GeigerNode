#include <avr/eeprom.h>
#include <avr/io.h>
#include <stdint.h>

#include "adc.h"
#include "board.h"
#include "hv.h"

#if F_CPU != 8000000UL
#error "HV pulse counts are calculated for F_CPU=8000000UL"
#endif

#define HV_COUNTS_PER_US (F_CPU / 1000000UL)

#if HV_MAX_PULSE_US != 100
#error "HV pulse cap is 100 us"
#endif
#if HV_FREQ_SERIES_MAX_HZ != 3000
#error "HV frequency characterization stops at 3000 Hz"
#endif
#if HV_ADC_HARD_ABORT != 932
#error "HV production abort is 932 counts, around 400 V"
#endif
#if HV_BENCH_VOLTS != 380 || HV_BENCH_HZ != 2500
#error "Production hold is 380 V at 2500 Hz"
#endif
#if HV_TARGET_V != 380 || HV_SWITCH_FREQ_HZ != 2400
#error "SI-29BG bring-up is 380 V at 2400 Hz"
#endif
#if HV_FREQ_TEST_COUNT != 6
#error "HV series is six tests"
#endif
#if HV_FREQ_TEST_0_VOLTS != 400 || HV_FREQ_TEST_1_VOLTS != 400 || \
	HV_FREQ_TEST_2_VOLTS != 400 || HV_FREQ_TEST_3_VOLTS != 400 || \
	HV_FREQ_TEST_4_VOLTS != 400 || HV_FREQ_TEST_5_VOLTS != 400
#error "Every test in this series targets 400 V"
#endif
#if HV_FREQ_TEST_0_HZ != 2500 || HV_FREQ_TEST_1_HZ != 2600 || \
	HV_FREQ_TEST_2_HZ != 2700 || HV_FREQ_TEST_3_HZ != 2800 || \
	HV_FREQ_TEST_4_HZ != 2900 || HV_FREQ_TEST_5_HZ != 3000
#error "This series steps from 2500 Hz to 3000 Hz"
#endif

_Static_assert(HV_COUNTS_PER_US == 8UL, "pulse ticks are 8 per us");
_Static_assert(sizeof(struct hv_freq_result) == 28U, "freq result is 14 words");
_Static_assert(sizeof(struct hv_freq_series) == 178U, "freq series record is 178 bytes");
_Static_assert(HV_MAX_PULSE_US * HV_COUNTS_PER_US < (F_CPU / HV_FREQ_SERIES_MAX_HZ) - 1UL,
	       "100 us compare value stays below 3000 Hz TOP");
_Static_assert((HV_MAX_PULSE_US * HV_COUNTS_PER_US) * 100UL < 31UL * (F_CPU / 3000UL),
	       "100 us stays under 31 percent duty at 3000 Hz");
_Static_assert((HV_MAX_PULSE_US * HV_COUNTS_PER_US) * 10UL == (F_CPU / 1000UL),
	       "100 us is 10 percent duty at 1000 Hz");
_Static_assert((F_CPU / 600UL) - 1UL == 13332UL, "600 Hz TOP is 13332");
_Static_assert((F_CPU / 750UL) - 1UL == 10665UL, "750 Hz TOP is 10665");
_Static_assert((F_CPU / 1000UL) - 1UL == 7999UL, "1000 Hz TOP is 7999");
_Static_assert((F_CPU / 1200UL) - 1UL == 6665UL, "1200 Hz TOP is 6665");
_Static_assert((F_CPU / 1400UL) - 1UL == 5713UL, "1400 Hz TOP is 5713");
_Static_assert((F_CPU / 1500UL) - 1UL == 5332UL, "1500 Hz TOP is 5332");
_Static_assert((F_CPU / 1600UL) - 1UL == 4999UL, "1600 Hz TOP is 4999");
_Static_assert((F_CPU / 2000UL) - 1UL == 3999UL, "2000 Hz TOP is 3999");
_Static_assert((F_CPU / 2100UL) - 1UL == 3808UL, "2100 Hz TOP is 3808");
_Static_assert((F_CPU / 2200UL) - 1UL == 3635UL, "2200 Hz TOP is 3635");
_Static_assert((F_CPU / 2300UL) - 1UL == 3477UL, "2300 Hz TOP is 3477");
_Static_assert((F_CPU / 2400UL) - 1UL == 3332UL, "2400 Hz TOP is 3332");
_Static_assert((F_CPU / 2500UL) - 1UL == 3199UL, "2500 Hz TOP is 3199");
_Static_assert((F_CPU / 2600UL) - 1UL == 3075UL, "2600 Hz TOP is 3075");
_Static_assert((F_CPU / 2700UL) - 1UL == 2961UL, "2700 Hz TOP is 2961");
_Static_assert((F_CPU / 2800UL) - 1UL == 2856UL, "2800 Hz TOP is 2856");
_Static_assert((F_CPU / 2900UL) - 1UL == 2757UL, "2900 Hz TOP is 2757");
_Static_assert((F_CPU / 3000UL) - 1UL == 2665UL, "3000 Hz TOP is 2665");
_Static_assert(((275000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 641ULL,
	       "275 V is 641 counts");
_Static_assert(((300000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 699ULL,
	       "300 V is 699 counts");
_Static_assert(((310000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 722ULL,
	       "310 V is 722 counts");
_Static_assert(((320000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 746ULL,
	       "320 V is 746 counts");
_Static_assert(((330000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 769ULL,
	       "330 V is 769 counts");
_Static_assert(722 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "310 V overshoot stays under the abort");
_Static_assert(746 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "320 V overshoot stays under the abort");
_Static_assert(769 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "330 V overshoot stays under the abort");
_Static_assert(((340000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 792ULL,
	       "340 V is 792 counts");
_Static_assert(((350000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 815ULL,
	       "350 V is 815 counts");
_Static_assert(((360000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 839ULL,
	       "360 V is 839 counts");
_Static_assert(((370000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 862ULL,
	       "370 V is 862 counts");
_Static_assert(((380000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 885ULL,
	       "380 V is 885 counts");
_Static_assert(((390000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 909ULL,
	       "390 V is 909 counts");
_Static_assert(((400000ULL * 1023ULL) + ((401ULL * 1095ULL) / 2ULL)) /
		       (401ULL * 1095ULL) == 932ULL,
	       "400 V is 932 counts");
_Static_assert(792 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "340 V overshoot stays under the abort");
_Static_assert(815 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "350 V overshoot stays under the abort");
_Static_assert(839 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "360 V overshoot stays under the abort");
_Static_assert(862 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "370 V overshoot stays under the abort");
_Static_assert(885 + HV_OVERSHOOT_COUNTS < HV_ADC_HARD_ABORT, "380 V overshoot stays under the abort");
_Static_assert(HV_ADC_HARD_ABORT == 932U, "production abort is the 400 V count");

volatile uint16_t adc_raw;
volatile uint16_t adc_avg;
volatile uint16_t adc_max;
volatile uint16_t target_adc;
volatile uint16_t pulse_width_us;
volatile uint16_t max_pulse_width_us;
volatile uint16_t active_frequency_hz;
volatile uint32_t estimated_hv_mv;
volatile uint32_t max_estimated_hv_mv;
volatile uint8_t regulator_state = HV_OFF;
volatile uint16_t status_flags;

static uint16_t started_ms;
static uint16_t last_ctrl_ms;
static uint16_t baseline_adc;
static uint16_t active_target_volts;
static uint16_t latched_final_pulse_us;
static uint16_t latched_status;
static uint16_t min_adc_settled;
static uint16_t min_pulse_settled_us;
static uint16_t updates_at_cap;
static uint16_t milliseconds_at_cap;
static uint8_t overshoot_hold;
static uint8_t sat_ticks;
static uint8_t settle_open;

static struct hv_freq_series series;
static uint8_t series_phase;
static uint8_t test_index;
static uint8_t delay_marked;
static uint16_t phase_ms;

enum {
	SERIES_IDLE = 0,
	SERIES_DELAY,
	SERIES_ACTIVE,
	SERIES_GAP,
	SERIES_DONE,
	SERIES_FAULT
};

static const uint16_t freq_volts[HV_FREQ_TEST_COUNT] = {
	HV_FREQ_TEST_0_VOLTS,
	HV_FREQ_TEST_1_VOLTS,
	HV_FREQ_TEST_2_VOLTS,
	HV_FREQ_TEST_3_VOLTS,
	HV_FREQ_TEST_4_VOLTS,
	HV_FREQ_TEST_5_VOLTS
};

static const uint16_t freq_hz[HV_FREQ_TEST_COUNT] = {
	HV_FREQ_TEST_0_HZ,
	HV_FREQ_TEST_1_HZ,
	HV_FREQ_TEST_2_HZ,
	HV_FREQ_TEST_3_HZ,
	HV_FREQ_TEST_4_HZ,
	HV_FREQ_TEST_5_HZ
};

static void hv_disconnect(void)
{
	TCCR1A = 0;
	TCCR1B = 0;
	OCR1A = 0;
	HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
	HV_SW_DDR |= _BV(HV_SW_DDR_BIT);
	pulse_width_us = 0;
}

static uint16_t hv_top_for(uint16_t frequency_hz)
{
	return (uint16_t)((F_CPU / frequency_hz) - 1UL);
}

static void hv_apply_width(uint16_t us)
{
	uint16_t ticks;

	if (us > HV_MAX_PULSE_US) {
		us = HV_MAX_PULSE_US;
	}
	pulse_width_us = us;
	if (us > max_pulse_width_us) {
		max_pulse_width_us = us;
	}
	if (us == 0) {
		/* OCR1A of 0 in mode 14 is a spike. Disconnect OC1A instead. */
		TCCR1A = _BV(WGM11);
		OCR1A = 0;
		HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
		return;
	}
	ticks = (uint16_t)(us * HV_COUNTS_PER_US);
	if (ICR1 < 2U || ticks >= ICR1 || ticks > (uint16_t)(HV_MAX_PULSE_US * HV_COUNTS_PER_US)) {
		hv_disconnect();
		latched_status = HV_TEST_FAULT;
		regulator_state = HV_FAULT;
		return;
	}
	OCR1A = ticks;
	TCCR1A = _BV(COM1A1) | _BV(WGM11);
}

static void hv_widen(uint16_t step)
{
	uint16_t next = pulse_width_us;

	if (next > (uint16_t)(HV_MAX_PULSE_US - step)) {
		next = HV_MAX_PULSE_US;
	} else {
		next = (uint16_t)(next + step);
	}
	hv_apply_width(next);
}

static void hv_narrow(uint16_t step)
{
	uint16_t next = pulse_width_us;

	if (next <= step) {
		next = 0;
	} else {
		next = (uint16_t)(next - step);
	}
	hv_apply_width(next);
}

static void hv_sample(void)
{
	adc_avg = adc_read_hv_raw();
	adc_raw = adc_hv_last_raw();
	if (adc_raw > adc_max) {
		adc_max = adc_raw;
	}
	if (adc_avg > adc_max) {
		adc_max = adc_avg;
	}
	estimated_hv_mv = hv_adc_to_mv(adc_avg);
	if (estimated_hv_mv > max_estimated_hv_mv) {
		max_estimated_hv_mv = estimated_hv_mv;
	}
}

static void hv_track(void)
{
	uint16_t error;

	if (pulse_width_us == HV_MAX_PULSE_US) {
		if (updates_at_cap < 65535U) {
			updates_at_cap++;
		}
		milliseconds_at_cap = (uint16_t)(updates_at_cap * HV_CTRL_PERIOD_MS);
	}
	if (regulator_state != HV_REGULATING) {
		return;
	}
	if (adc_avg > target_adc) {
		error = (uint16_t)(adc_avg - target_adc);
	} else {
		error = (uint16_t)(target_adc - adc_avg);
	}
	if (!settle_open &&
	    (error <= HV_OK_BAND_COUNTS || pulse_width_us == HV_MAX_PULSE_US)) {
		settle_open = 1;
	}
	if (!settle_open) {
		return;
	}
	if (adc_avg < min_adc_settled) {
		min_adc_settled = adc_avg;
	}
	if (pulse_width_us > 0 && pulse_width_us < min_pulse_settled_us) {
		min_pulse_settled_us = pulse_width_us;
	}
}

static void hv_regulate(void)
{
	if (overshoot_hold || adc_avg > (uint16_t)(target_adc + HV_OVERSHOOT_COUNTS)) {
		overshoot_hold = 1;
		status_flags |= HV_FLAG_OVERSHOOT;
		hv_apply_width(0);
		if (adc_avg <= (uint16_t)(target_adc + HV_HYST_COUNTS)) {
			overshoot_hold = 0;
		}
		return;
	}

	if ((uint16_t)(adc_avg + HV_FAST_LOW_COUNTS) < target_adc) {
		hv_widen(2);
	} else if ((uint16_t)(adc_avg + HV_HYST_COUNTS) < target_adc) {
		hv_widen(1);
	} else if (adc_avg > (uint16_t)(target_adc + HV_FAST_HIGH_COUNTS)) {
		hv_narrow(3);
	} else if (adc_avg > (uint16_t)(target_adc + HV_HYST_COUNTS)) {
		hv_narrow(1);
	}

	if (regulator_state == HV_FAULT) {
		return;
	}
	if (pulse_width_us == HV_MAX_PULSE_US &&
	    target_adc > adc_avg &&
	    (uint16_t)(target_adc - adc_avg) > HV_FAST_LOW_COUNTS) {
		if (sat_ticks < 255) {
			sat_ticks++;
		}
		if (sat_ticks >= HV_SAT_TICKS) {
			status_flags |= HV_FLAG_SATURATED;
		}
	} else {
		sat_ticks = 0;
	}
}

static uint16_t hv_complete_status(void)
{
	uint16_t error;

	if (status_flags & HV_FLAG_SATURATED) {
		return HV_TEST_SATURATED;
	}
	if (pulse_width_us == HV_MAX_PULSE_US &&
	    (uint16_t)(adc_avg + HV_HYST_COUNTS) < target_adc) {
		return HV_TEST_SATURATED;
	}
	if (adc_avg > target_adc) {
		error = (uint16_t)(adc_avg - target_adc);
	} else {
		error = (uint16_t)(target_adc - adc_avg);
	}
	if (error > HV_OK_BAND_COUNTS) {
		return HV_TEST_TIMEOUT;
	}
	return HV_TEST_OK;
}

static void hv_finish(uint8_t state, uint16_t status)
{
	latched_final_pulse_us = pulse_width_us;
	hv_disconnect();
	latched_status = status;
	regulator_state = state;
}

static void hv_step(void)
{
	hv_sample();
	if (adc_avg >= HV_ADC_HARD_ABORT || adc_raw >= HV_ADC_HARD_ABORT) {
		hv_finish(HV_FAULT, HV_TEST_OVERVOLTAGE);
		return;
	}
	/*
	 * This image may regulate only the SI-29BG point. A wrong TOP or
	 * a wrong commanded rate latches off. It does not retune either one.
	 */
	if (active_frequency_hz != HV_SWITCH_FREQ_HZ ||
	    ICR1 != hv_top_for(HV_SWITCH_FREQ_HZ)) {
		hv_finish(HV_FAULT, HV_TEST_FAULT);
		return;
	}
	if (regulator_state == HV_STARTING) {
		if (adc_avg > (uint16_t)(baseline_adc + HV_RISE_COUNTS)) {
			regulator_state = HV_REGULATING;
			status_flags |= HV_FLAG_RISING;
		}
		return;
	}
	hv_regulate();
	if (regulator_state == HV_FAULT) {
		return;
	}
	hv_track();
	if (pulse_width_us > HV_MAX_PULSE_US ||
	    OCR1A > (uint16_t)(HV_MAX_PULSE_US * HV_COUNTS_PER_US)) {
		hv_finish(HV_FAULT, HV_TEST_FAULT);
	}
}

static uint8_t hv_test_begin(uint16_t now_ms, uint16_t volts, uint16_t frequency_hz)
{
	uint16_t ticks;
	uint16_t top;

	/*
	 * Refuse every other operating point before the timer starts.
	 * The characterization series calls this with higher rates; this
	 * image does not invoke that series, and those calls cannot start HV.
	 */
	if (regulator_state != HV_OFF || volts != HV_TARGET_V ||
	    frequency_hz != HV_SWITCH_FREQ_HZ) {
		hv_disconnect();
		return 0;
	}
	top = hv_top_for(frequency_hz);
	ticks = (uint16_t)(HV_MAX_PULSE_US * HV_COUNTS_PER_US);
	if (top < 2U || ticks >= top) {
		hv_disconnect();
		return 0;
	}

	baseline_adc = adc_read_hv_raw();
	adc_raw = adc_hv_last_raw();
	adc_avg = baseline_adc;
	adc_max = adc_avg;
	active_target_volts = volts;
	target_adc = hv_mv_to_adc((uint32_t)volts * 1000UL);
	active_frequency_hz = frequency_hz;
	estimated_hv_mv = hv_adc_to_mv(adc_avg);
	max_estimated_hv_mv = estimated_hv_mv;
	max_pulse_width_us = 0;
	latched_final_pulse_us = 0;
	latched_status = 0;
	min_adc_settled = 0xFFFFU;
	min_pulse_settled_us = 0xFFFFU;
	updates_at_cap = 0;
	milliseconds_at_cap = 0;
	status_flags = 0;
	overshoot_hold = 0;
	sat_ticks = 0;
	settle_open = 0;
	started_ms = now_ms;
	last_ctrl_ms = now_ms;

	TCCR1A = 0;
	TCCR1B = 0;
	TCNT1 = 0;
	ICR1 = top;
	hv_apply_width(HV_START_PULSE_US);
	if (regulator_state == HV_FAULT || pulse_width_us > HV_MAX_PULSE_US) {
		hv_disconnect();
		regulator_state = HV_FAULT;
		return 0;
	}
	TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
	regulator_state = HV_STARTING;
	return 1;
}

static void hv_poll(uint16_t now_ms)
{
	if (regulator_state != HV_STARTING && regulator_state != HV_REGULATING) {
		return;
	}
	if ((uint16_t)(now_ms - started_ms) >= HV_TEST_MAX_RUNTIME_MS) {
		hv_finish(HV_COMPLETE, hv_complete_status());
		return;
	}
	if ((uint16_t)(now_ms - last_ctrl_ms) < HV_CTRL_PERIOD_MS) {
		return;
	}
	last_ctrl_ms = now_ms;
	hv_step();
}

void hv_bench_boot(void)
{
	hv_disconnect();
	regulator_state = HV_OFF;
	active_frequency_hz = 0;
}

uint8_t hv_bench_start(uint16_t now_ms)
{
	if (hv_test_begin(now_ms, HV_BENCH_VOLTS, HV_BENCH_HZ)) {
		return 1;
	}
	hv_disconnect();
	regulator_state = HV_FAULT;
	return 0;
}

void hv_bench_poll(uint16_t now_ms)
{
	if (regulator_state != HV_STARTING && regulator_state != HV_REGULATING) {
		return;
	}
	if (regulator_state == HV_STARTING &&
	    (uint16_t)(now_ms - started_ms) >= HV_TEST_MAX_RUNTIME_MS) {
		hv_finish(HV_FAULT, HV_TEST_TIMEOUT);
		return;
	}
	if ((uint16_t)(now_ms - last_ctrl_ms) < HV_CTRL_PERIOD_MS) {
		return;
	}
	last_ctrl_ms = now_ms;
	hv_step();
	if (regulator_state == HV_REGULATING && (status_flags & HV_FLAG_SATURATED)) {
		hv_finish(HV_FAULT, HV_TEST_SATURATED);
	}
}

uint8_t hv_bench_indicator(void)
{
	if (regulator_state == HV_FAULT) {
		return HV_LED_FAULT;
	}
	if (regulator_state == HV_STARTING || regulator_state == HV_REGULATING) {
		return HV_LED_ON;
	}
	return HV_LED_OFF;
}

uint8_t hv_switch_is_off(void)
{
	uint8_t com = (uint8_t)(TCCR1A & (_BV(COM1A1) | _BV(COM1A0)));
	uint8_t clock = (uint8_t)(TCCR1B & (_BV(CS12) | _BV(CS11) | _BV(CS10)));

	return pulse_width_us == 0 && com == 0 && clock == 0 &&
	       (HV_SW_DDR & _BV(HV_SW_DDR_BIT)) != 0 &&
	       (HV_SW_PORT & _BV(HV_SW_PORT_BIT)) == 0;
}

uint16_t hv_latched_pulse_us(void)
{
	return latched_final_pulse_us;
}

uint16_t hv_latched_status(void)
{
	return latched_status;
}

uint32_t hv_cap_milliseconds(void)
{
	return (uint32_t)updates_at_cap * (uint32_t)HV_CTRL_PERIOD_MS;
}

uint8_t hv_gm_begin(uint16_t now_ms)
{
	return hv_test_begin(now_ms, HV_TARGET_V, HV_SWITCH_FREQ_HZ);
}

void hv_gm_poll(uint16_t now_ms)
{
	if (regulator_state != HV_STARTING && regulator_state != HV_REGULATING) {
		return;
	}
	if (regulator_state == HV_STARTING &&
	    (uint16_t)(now_ms - started_ms) >= HV_TEST_MAX_RUNTIME_MS) {
		hv_finish(HV_FAULT, HV_TEST_TIMEOUT);
		return;
	}
	if ((uint16_t)(now_ms - last_ctrl_ms) < HV_CTRL_PERIOD_MS) {
		return;
	}
	last_ctrl_ms = now_ms;
	hv_step();
}

void hv_gm_stop(void)
{
	if (regulator_state == HV_STARTING || regulator_state == HV_REGULATING) {
		latched_final_pulse_us = pulse_width_us;
	}
	hv_disconnect();
	if (regulator_state != HV_FAULT) {
		regulator_state = HV_OFF;
	}
	active_frequency_hz = 0;
}

uint8_t hv_is_starting(void)
{
	return regulator_state == HV_STARTING;
}

uint8_t hv_is_regulating(void)
{
	return regulator_state == HV_REGULATING;
}

uint8_t hv_is_fault(void)
{
	return regulator_state == HV_FAULT;
}

static void hv_store_series(void)
{
	series.checksum = hv_freq_checksum(&series);
	eeprom_update_block(&series, (void *)0, sizeof series);
}

static void hv_clear_results(void)
{
	uint8_t i;
	uint8_t *raw = (uint8_t *)series.result;

	for (i = 0; i < sizeof series.result; i++) {
		raw[i] = 0;
	}
	series.completed_tests = 0;
}

static uint8_t hv_output_is_off(void)
{
	uint8_t com = (uint8_t)(TCCR1A & (_BV(COM1A1) | _BV(COM1A0)));
	uint8_t clock = (uint8_t)(TCCR1B & (_BV(CS12) | _BV(CS11) | _BV(CS10)));

	return regulator_state == HV_OFF && pulse_width_us == 0 && com == 0 &&
	       clock == 0 && (HV_SW_DDR & _BV(HV_SW_DDR_BIT)) != 0 &&
	       (HV_SW_PORT & _BV(HV_SW_PORT_BIT)) == 0;
}

static void hv_fill_result(uint16_t status)
{
	struct hv_freq_result *row;

	if (test_index >= HV_FREQ_TEST_COUNT) {
		return;
	}
	row = &series.result[test_index];
	row->target_volts = active_target_volts;
	row->target_adc = target_adc;
	row->frequency_hz = active_frequency_hz;
	row->final_adc = adc_avg;
	row->max_adc = adc_max;
	row->min_adc_settled = min_adc_settled;
	row->final_hv_volts = (uint16_t)(estimated_hv_mv / 1000UL);
	row->max_hv_volts = (uint16_t)(max_estimated_hv_mv / 1000UL);
	row->final_pulse_us = latched_final_pulse_us;
	row->max_pulse_us = max_pulse_width_us;
	row->min_pulse_settled_us = min_pulse_settled_us;
	row->control_updates_at_cap = updates_at_cap;
	row->milliseconds_at_cap = milliseconds_at_cap;
	row->status = status;
}

static void hv_commit(uint16_t now_ms, uint8_t stop)
{
	hv_fill_result(latched_status);
	if (series.completed_tests < HV_FREQ_TEST_COUNT) {
		series.completed_tests++;
	}
	if (stop || test_index + 1U >= HV_FREQ_TEST_COUNT) {
		series.series_state = stop ? HV_FREQ_STATE_ABORTED : HV_FREQ_STATE_COMPLETE;
		hv_store_series();
		active_frequency_hz = 0;
		regulator_state = HV_OFF;
		series_phase = stop ? SERIES_FAULT : SERIES_DONE;
		return;
	}
	series.series_state = HV_FREQ_STATE_RUNNING;
	hv_store_series();
	active_frequency_hz = 0;
	regulator_state = HV_OFF;
	phase_ms = now_ms;
	series_phase = SERIES_GAP;
}

static void hv_abort_now(uint16_t now_ms)
{
	if (regulator_state == HV_STARTING || regulator_state == HV_REGULATING) {
		hv_finish(HV_FAULT, HV_TEST_FAULT);
		hv_commit(now_ms, 1);
		return;
	}
	hv_disconnect();
	regulator_state = HV_OFF;
	active_frequency_hz = 0;
	series.series_state = HV_FREQ_STATE_ABORTED;
	hv_store_series();
	series_phase = SERIES_FAULT;
	(void)now_ms;
}

static uint8_t hv_freq_is_blank(const struct hv_freq_series *record)
{
	return record->magic == 0xFFFFU && record->version == 0xFFFFU &&
	       record->series_state == 0xFFFFU;
}

static uint8_t hv_freq_is_valid(const struct hv_freq_series *record)
{
	return record->magic == HV_FREQ_MAGIC && record->version == HV_FREQ_VERSION &&
	       record->checksum == hv_freq_checksum(record);
}

static void hv_arm_running(void)
{
	series.magic = HV_FREQ_MAGIC;
	series.version = HV_FREQ_VERSION;
	hv_clear_results();
	series.series_state = HV_FREQ_STATE_RUNNING;
	hv_store_series();
	test_index = 0;
	delay_marked = 0;
	series_phase = SERIES_DELAY;
}

void hv_series_boot(void)
{
	hv_disconnect();
	regulator_state = HV_OFF;
	active_frequency_hz = 0;
	eeprom_read_block(&series, (const void *)0, sizeof series);

	if (hv_freq_is_blank(&series)) {
		hv_arm_running();
		return;
	}
	if (!hv_freq_is_valid(&series)) {
		series_phase = SERIES_IDLE;
		return;
	}
	if (series.series_state == HV_FREQ_STATE_ARMED) {
		hv_arm_running();
		return;
	}
	if (series.series_state == HV_FREQ_STATE_RUNNING) {
		series.series_state = HV_FREQ_STATE_ABORTED;
		hv_store_series();
		series_phase = SERIES_FAULT;
		return;
	}
	if (series.series_state == HV_FREQ_STATE_COMPLETE) {
		series_phase = SERIES_DONE;
		return;
	}
	if (series.series_state == HV_FREQ_STATE_ABORTED) {
		series_phase = SERIES_FAULT;
		return;
	}
	series_phase = SERIES_IDLE;
}

void hv_series_poll(uint16_t now_ms)
{
	if (series_phase == SERIES_DELAY || series_phase == SERIES_GAP ||
	    series_phase == SERIES_DONE || series_phase == SERIES_FAULT ||
	    series_phase == SERIES_IDLE) {
		if (!hv_output_is_off()) {
			hv_disconnect();
			regulator_state = HV_OFF;
			if (series_phase == SERIES_DELAY || series_phase == SERIES_GAP) {
				hv_abort_now(now_ms);
			} else if (series_phase != SERIES_FAULT) {
				series_phase = SERIES_FAULT;
			}
		}
	}

	if (series_phase == SERIES_DELAY) {
		if (!delay_marked) {
			phase_ms = now_ms;
			delay_marked = 1;
		}
		if ((uint16_t)(now_ms - phase_ms) < HV_SERIES_ARM_DELAY_MS) {
			return;
		}
		if (!hv_test_begin(now_ms, freq_volts[0], freq_hz[0])) {
			latched_status = HV_TEST_FAULT;
			active_target_volts = freq_volts[0];
			active_frequency_hz = freq_hz[0];
			hv_abort_now(now_ms);
			return;
		}
		series_phase = SERIES_ACTIVE;
		return;
	}

	if (series_phase == SERIES_ACTIVE) {
		hv_poll(now_ms);
		if (regulator_state == HV_STARTING || regulator_state == HV_REGULATING) {
			return;
		}
		if (regulator_state == HV_COMPLETE) {
			hv_commit(now_ms, 0);
			return;
		}
		if (regulator_state != HV_FAULT) {
			latched_status = HV_TEST_FAULT;
			hv_finish(HV_FAULT, HV_TEST_FAULT);
		}
		hv_commit(now_ms, 1);
		return;
	}

	if (series_phase == SERIES_GAP) {
		if ((uint16_t)(now_ms - phase_ms) < HV_SERIES_OFF_MS) {
			return;
		}
		test_index++;
		if (test_index >= HV_FREQ_TEST_COUNT ||
		    !hv_test_begin(now_ms, freq_volts[test_index], freq_hz[test_index])) {
			latched_status = HV_TEST_FAULT;
			hv_abort_now(now_ms);
			return;
		}
		series_phase = SERIES_ACTIVE;
	}
}

uint8_t hv_series_indicator(void)
{
	if (series_phase == SERIES_DELAY) {
		return HV_LED_FAST;
	}
	if (series_phase == SERIES_ACTIVE || series_phase == SERIES_DONE) {
		return HV_LED_ON;
	}
	if (series_phase == SERIES_GAP) {
		return HV_LED_SLOW;
	}
	if (series_phase == SERIES_FAULT) {
		return HV_LED_FAULT;
	}
	return HV_LED_OFF;
}
