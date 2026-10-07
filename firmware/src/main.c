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

#if F_CPU != 8000000UL
#error "The 1 ms tick is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 CTC, prescaler 64, OCR2A 124. 8000000 / (64 * 125) = 1000 Hz.
 * The tick ISR only counts. INT0 owns the GM edges. The bit-bang slave
 * owns the bus. Main owns HV, dose conversion, and the LED.
 */
_Static_assert(F_CPU / 64UL / 125UL == 1000UL, "Timer2 CTC is 1 ms");
_Static_assert(RADIATION_SI29BG_SENSITIVITY_REG == 138U,
	       "SI-29BG RadSens register defaults to 138 impulses/uR");
_Static_assert(RADIATION_RADSENS_MAX == 1440000UL,
	       "RadSens intensity saturates at 144000.0 uR/h");
_Static_assert(TWI_REG_COUNT > TWI_REG_LED, "RadSens map includes 0x14");

#define APP_PRE_HV_MS       500UL
#define APP_PUBLISH_MS      1000UL
#define APP_STATIC_S        500U
#define APP_DYNAMIC_MAX     122U
#define APP_DYNAMIC_RECENT  10U
#define APP_SHRINK_COUNTS   4U
#define GM_PULSE_LED_MS     40UL

_Static_assert(APP_DYNAMIC_MAX < APP_STATIC_S, "dynamic window fits in the static buffer");
_Static_assert(APP_DYNAMIC_RECENT < APP_DYNAMIC_MAX, "shrink leaves an older slice");

#define APP_DELAY  0U
#define APP_SETTLE 1U
#define APP_RUN    2U
#define APP_FAULT  3U
#define APP_IDLE   4U

static uint16_t cps_buckets[APP_STATIC_S];
static uint16_t cps_pos;
static uint16_t cps_len;
static uint32_t stat_sum;
static uint32_t dyn_sum;
static uint8_t dyn_len;

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
	static const uint16_t dwell_ms[9] = {
		100, 100, 100, 100, 100, 100, 100, 100, 700
	};
	static const uint8_t level[9] = {
		1, 0, 1, 0, 1, 0, 1, 0, 0
	};

	if ((now_ms - *last_ms) < dwell_ms[*step]) {
		return;
	}
	*last_ms = now_ms;
	*step = (uint8_t)((*step + 1U) % 9U);
	gpio_led_set(level[*step]);
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

	for (i = 0; i < APP_STATIC_S; i++) {
		cps_buckets[i] = 0;
	}
	cps_pos = 0;
	cps_len = 0;
	stat_sum = 0;
	dyn_sum = 0;
	dyn_len = 0;
}

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
			idx = APP_STATIC_S;
		}
		idx--;
		sum += cps_buckets[idx];
	}
	return sum;
}

static void windows_push(uint16_t delta)
{
	uint32_t recent;
	uint32_t older;
	uint16_t older_len;
	uint32_t recent_scaled;
	uint32_t older_scaled;
	uint32_t busier;

	if (cps_len == APP_STATIC_S) {
		stat_sum -= cps_buckets[cps_pos];
	}
	if (dyn_len == APP_DYNAMIC_MAX) {
		uint16_t drop = (uint16_t)((cps_pos + APP_STATIC_S - APP_DYNAMIC_MAX) %
					   APP_STATIC_S);

		dyn_sum -= cps_buckets[drop];
	}
	cps_buckets[cps_pos] = delta;
	stat_sum += delta;
	dyn_sum += delta;
	cps_pos = (uint16_t)((cps_pos + 1U) % APP_STATIC_S);
	if (cps_len < APP_STATIC_S) {
		cps_len++;
	}
	if (dyn_len < APP_DYNAMIC_MAX) {
		dyn_len++;
	}
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

static void publish_rates(void)
{
	uint16_t sens = radsens_sensitivity();
	uint32_t dynamic_x10;
	uint32_t static_x10;

	dynamic_x10 = radiation_counts_to_urh_x10(dyn_sum,
		(uint32_t)dyn_len * APP_PUBLISH_MS, sens);
	static_x10 = radiation_counts_to_urh_x10(stat_sum,
		(uint32_t)cps_len * APP_PUBLISH_MS, sens);
	radsens_publish_radiation(dynamic_x10, static_x10);
}

static void enter_fault(uint8_t *app)
{
	gm_counter_close_window();
	radsens_on_fault();
	radsens_publish_radiation(0, 0);
	*app = APP_FAULT;
}

static void enter_idle(uint8_t *app)
{
	hv_gm_stop();
	gm_counter_close_window();
	radsens_publish_radiation(0, 0);
	gpio_led_set(0);
	*app = APP_IDLE;
}

static void enter_settle(uint32_t now_ms, uint32_t *phase_ms, uint32_t *settle_ok_ms,
			 uint8_t *app)
{
	if (hv_gm_begin((uint16_t)now_ms)) {
		*phase_ms = now_ms;
		*settle_ok_ms = 0;
		gpio_led_set(0);
		*app = APP_SETTLE;
	} else {
		enter_fault(app);
	}
}

static void enter_run(uint32_t now_ms, uint32_t *last_publish_ms,
		      uint32_t *counted_window, uint32_t *bucket_window,
		      uint8_t *app)
{
	gm_counter_open_window();
	*counted_window = 0;
	*bucket_window = 0;
	*last_publish_ms = now_ms;
	windows_clear();
	twi_slave_clear_pulses();
	radsens_publish_radiation(0, 0);
	*app = APP_RUN;
}

int main(void)
{
	uint32_t last_toggle_ms = 0;
	uint32_t pulse_on_until = 0;
	uint32_t phase_ms = 0;
	uint32_t settle_ok_ms = 0;
	uint32_t last_publish_ms = 0;
	uint32_t counted_window = 0;
	uint32_t bucket_window = 0;
	uint8_t fault_step = 0;
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
	sei();

	for (;;) {
		uint32_t now_ms = time_ms();
		uint8_t hv_cmd;

		radsens_poll();
		hv_cmd = radsens_hv_command();

		if (app == APP_DELAY) {
			gpio_led_set(radsens_led_enabled());
			if (now_ms >= APP_PRE_HV_MS) {
				if (hv_cmd) {
					enter_settle(now_ms, &phase_ms, &settle_ok_ms, &app);
				} else {
					gpio_led_set(0);
					app = APP_IDLE;
				}
			}
		} else if (app == APP_IDLE) {
			gpio_led_set(0);
			if (hv_cmd) {
				enter_settle(now_ms, &phase_ms, &settle_ok_ms, &app);
			}
		} else if (app == APP_FAULT) {
			led_fault(now_ms, &last_toggle_ms, &fault_step);
		} else if (!hv_cmd) {
			enter_idle(&app);
		} else {
			hv_gm_poll((uint16_t)now_ms);
			if (hv_is_fault()) {
				enter_fault(&app);
			} else if (app == APP_SETTLE) {
				if ((now_ms - phase_ms) >= HV_SETTLE_TIMEOUT_MS) {
					hv_gm_stop();
					enter_fault(&app);
				} else if (hv_is_regulating() && hv_in_band()) {
					if (settle_ok_ms == 0UL) {
						settle_ok_ms = now_ms;
					} else if ((now_ms - settle_ok_ms) >= HV_SETTLE_HOLD_MS) {
						enter_run(now_ms, &last_publish_ms,
							  &counted_window, &bucket_window,
							  &app);
					}
				} else {
					settle_ok_ms = 0;
				}
			} else {
				uint32_t window = gm_counter_window();
				uint32_t add = window - counted_window;

				counted_window = window;
				if (add > 65535UL) {
					add = 65535UL;
				}
				twi_slave_count_pulses((uint16_t)add);
				if ((now_ms - last_publish_ms) >= APP_PUBLISH_MS) {
					uint32_t delta = window - bucket_window;

					last_publish_ms = now_ms;
					bucket_window = window;
					if (delta > 65535UL) {
						delta = 65535UL;
					}
					windows_push((uint16_t)delta);
					publish_rates();
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
