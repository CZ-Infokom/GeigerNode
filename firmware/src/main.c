#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>

#include "adc.h"
#include "gm_counter.h"
#include "gpio.h"
#include "hv.h"
#include "radsens.h"
#include "twi_slave.h"

#if F_CPU != 8000000UL
#error "The 1 ms tick is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 compare interrupt at 1 kHz: 8000000 / (64 * 125).
 * The tick ISR only counts. Main samples HV_ADC and updates OCR1A.
 * Production hold: PB1 stays low for 5 s, then Timer1 regulates 380 V
 * at 2500 Hz. A fault latches PB1 low with no restart.
 */
static volatile uint16_t milliseconds;

ISR(TIMER2_COMPA_vect)
{
	milliseconds++;
}

static void timer2_start_1ms(void)
{
	OCR2A = 124;
	TCCR2A = _BV(WGM21);
	TCCR2B = _BV(CS22);
	TIMSK2 = _BV(OCIE2A);
}

static void led_fault(uint16_t now_ms, uint16_t *last_ms, uint8_t *phase)
{
	static const uint16_t dwell_ms[9] = {
		100, 100, 100, 100, 100, 100, 100, 100, 700
	};
	static const uint8_t level[9] = {
		1, 0, 1, 0, 1, 0, 1, 0, 0
	};

	if ((uint16_t)(now_ms - *last_ms) < dwell_ms[*phase]) {
		return;
	}
	*last_ms = now_ms;
	*phase = (uint8_t)((*phase + 1U) % 9U);
	gpio_led_set(level[*phase]);
}

int main(void)
{
	uint16_t last_toggle_ms = 0;
	uint8_t fault_phase = 0;
	uint8_t last_mode = 0xFF;

	gpio_init();
	gm_counter_init();
	adc_init();
	twi_slave_init();
	radsens_init();
	timer2_start_1ms();
	sei();
	uint8_t bench_started = 0;

	hv_bench_boot();

	for (;;) {
		uint16_t now_ms;
		uint8_t mode;

		cli();
		now_ms = milliseconds;
		sei();

		if (!bench_started) {
			if (now_ms < HV_SERIES_ARM_DELAY_MS) {
				mode = HV_LED_FAST;
			} else if (!hv_bench_start(now_ms)) {
				bench_started = 1;
				mode = HV_LED_FAULT;
			} else {
				bench_started = 1;
				mode = HV_LED_ON;
			}
		} else {
			hv_bench_poll(now_ms);
			mode = hv_bench_indicator();
		}
		if (mode != last_mode) {
			last_mode = mode;
			last_toggle_ms = now_ms;
			fault_phase = 0;
			gpio_led_set(mode == HV_LED_ON || mode == HV_LED_FAULT);
			continue;
		}
		if (mode == HV_LED_ON) {
			gpio_led_set(1);
			continue;
		}
		if (mode == HV_LED_FAST) {
			if ((uint16_t)(now_ms - last_toggle_ms) >= 100) {
				last_toggle_ms = now_ms;
				gpio_led_toggle();
			}
			continue;
		}
		if (mode == HV_LED_SLOW) {
			if ((uint16_t)(now_ms - last_toggle_ms) >= 500) {
				last_toggle_ms = now_ms;
				gpio_led_toggle();
			}
			continue;
		}
		if (mode == HV_LED_FAULT) {
			led_fault(now_ms, &last_toggle_ms, &fault_phase);
			continue;
		}
		gpio_led_set(0);
	}
}
