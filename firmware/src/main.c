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
 * The tick ISR only counts. INT0 owns the GM edges. TWI0 owns the bus.
 * Main owns HV, dose conversion, and the LED.
 */
_Static_assert(F_CPU / 64UL / 125UL == 1000UL, "Timer2 CTC is 1 ms");
_Static_assert(RADIATION_SI29BG_SENSITIVITY_X10 == 1375U,
	       "SI-29BG conversion stays 137.5 impulses/uR");
_Static_assert(TWI_REG_COUNT > TWI_REG_LED, "RadSens map includes 0x14");

#define APP_PRE_HV_MS     5000UL
#define APP_PUBLISH_MS    1000UL
#define APP_DYNAMIC_S     10U
#define APP_STATIC_S      60U
#define GM_PULSE_LED_MS   40UL

#define APP_DELAY  0U
#define APP_SETTLE 1U
#define APP_RUN    2U
#define APP_FAULT  3U

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

static uint16_t saturate_pulses(uint32_t count)
{
	if (count > 65535UL) {
		return 65535U;
	}
	return (uint16_t)count;
}

int main(void)
{
	uint32_t last_toggle_ms = 0;
	uint32_t pulse_on_until = 0;
	uint32_t phase_ms = 0;
	uint32_t settle_ok_ms = 0;
	uint32_t last_publish_ms = 0;
	uint32_t last_total = 0;
	uint16_t dyn_cps[APP_DYNAMIC_S];
	uint16_t stat_cps[APP_STATIC_S];
	uint32_t dyn_sum = 0;
	uint32_t stat_sum = 0;
	uint8_t dyn_pos = 0;
	uint8_t dyn_len = 0;
	uint8_t stat_pos = 0;
	uint8_t stat_len = 0;
	uint8_t fault_step = 0;
	uint8_t app = APP_DELAY;
	uint8_t i;

	MCUSR = 0;
	for (i = 0; i < APP_DYNAMIC_S; i++) {
		dyn_cps[i] = 0;
	}
	for (i = 0; i < APP_STATIC_S; i++) {
		stat_cps[i] = 0;
	}
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
		uint8_t pulsed = gm_counter_take_pulse();

		if (app == APP_DELAY) {
			if (now_ms >= APP_PRE_HV_MS) {
				if (hv_gm_begin((uint16_t)now_ms)) {
					app = APP_SETTLE;
					phase_ms = now_ms;
					settle_ok_ms = 0;
				} else {
					app = APP_FAULT;
				}
			}
		} else if (app == APP_SETTLE || app == APP_RUN) {
			hv_gm_poll((uint16_t)now_ms);
			if (hv_is_fault()) {
				gm_counter_close_window();
				radsens_publish(0, 0, saturate_pulses(gm_counter_window()), 0);
				app = APP_FAULT;
			} else if (app == APP_SETTLE) {
				if ((now_ms - phase_ms) >= HV_SETTLE_TIMEOUT_MS) {
					hv_gm_stop();
					app = APP_FAULT;
				} else if (hv_is_regulating() && hv_in_band()) {
					if (settle_ok_ms == 0UL) {
						settle_ok_ms = now_ms;
					} else if ((now_ms - settle_ok_ms) >= HV_SETTLE_HOLD_MS) {
						gm_counter_open_window();
						last_total = gm_counter_total();
						last_publish_ms = now_ms;
						dyn_sum = 0;
						stat_sum = 0;
						dyn_pos = 0;
						dyn_len = 0;
						stat_pos = 0;
						stat_len = 0;
						for (i = 0; i < APP_DYNAMIC_S; i++) {
							dyn_cps[i] = 0;
						}
						for (i = 0; i < APP_STATIC_S; i++) {
							stat_cps[i] = 0;
						}
						app = APP_RUN;
					}
				} else {
					settle_ok_ms = 0;
				}
			} else if ((now_ms - last_publish_ms) >= APP_PUBLISH_MS) {
				uint32_t total = gm_counter_total();
				uint32_t delta = total - last_total;
				uint32_t dynamic_x10;
				uint32_t static_x10;
				uint16_t oldest;

				last_publish_ms = now_ms;
				last_total = total;
				if (delta > 65535UL) {
					delta = 65535UL;
				}
				oldest = (dyn_len == APP_DYNAMIC_S) ? dyn_cps[dyn_pos] : 0;
				dyn_sum -= oldest;
				dyn_cps[dyn_pos] = (uint16_t)delta;
				dyn_sum += (uint16_t)delta;
				dyn_pos++;
				if (dyn_pos >= APP_DYNAMIC_S) {
					dyn_pos = 0;
				}
				if (dyn_len < APP_DYNAMIC_S) {
					dyn_len++;
				}
				oldest = (stat_len == APP_STATIC_S) ? stat_cps[stat_pos] : 0;
				stat_sum -= oldest;
				stat_cps[stat_pos] = (uint16_t)delta;
				stat_sum += (uint16_t)delta;
				stat_pos++;
				if (stat_pos >= APP_STATIC_S) {
					stat_pos = 0;
				}
				if (stat_len < APP_STATIC_S) {
					stat_len++;
				}
				dynamic_x10 = radiation_counts_to_urh_x10(dyn_sum,
					(uint32_t)dyn_len * APP_PUBLISH_MS);
				static_x10 = radiation_counts_to_urh_x10(stat_sum,
					(uint32_t)stat_len * APP_PUBLISH_MS);
				radsens_publish(dynamic_x10, static_x10,
						saturate_pulses(gm_counter_window()), 1);
			}
		}

		if (app == APP_RUN) {
			if (pulsed) {
				pulse_on_until = now_ms + GM_PULSE_LED_MS;
			}
			gpio_led_set((int32_t)(pulse_on_until - now_ms) > 0);
			continue;
		}
		if (app == APP_FAULT) {
			led_fault(now_ms, &last_toggle_ms, &fault_step);
			continue;
		}
		if (app == APP_SETTLE) {
			if ((now_ms - last_toggle_ms) >= 100UL) {
				last_toggle_ms = now_ms;
				gpio_led_toggle();
			}
			continue;
		}
		if ((now_ms - last_toggle_ms) >= 500UL) {
			last_toggle_ms = now_ms;
			gpio_led_toggle();
		}
	}
}
