#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "adc.h"
#include "gm_counter.h"
#include "gm_test.h"
#include "gpio.h"
#include "hv.h"

#if F_CPU != 8000000UL
#error "The 1 ms tick is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 CTC, prescaler 64, OCR2A 124. 8000000 / (64 * 125) = 1000 Hz.
 * The tick ISR only counts. INT0 owns the GM edges. Main owns HV and the LED.
 */
_Static_assert(F_CPU / 64UL / 125UL == 1000UL, "Timer2 CTC is 1 ms");

#define GM_PULSE_LED_MS 40UL

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

int main(void)
{
	uint32_t last_toggle_ms = 0;
	uint32_t pulse_off_until = 0;
	uint8_t fault_step = 0;
	uint8_t last_mode = 0xFF;

	/*
	 * PORF stays set until software clears it. Capture the flags before
	 * any later reset can mix a programmer reset with an old power-on.
	 */
	uint8_t reset_flags = MCUSR;

	MCUSR = 0;
	/* PB1 low and OC1A disconnected before any other peripheral starts. */
	gpio_init();
	adc_init();
	timer2_start_1ms();
	gm_counter_init();
	gm_test_boot(reset_flags);
	sei();

	for (;;) {
		uint32_t now_ms = time_ms();
		uint8_t pulsed = gm_counter_take_pulse();
		uint8_t mode;

		gm_test_poll(now_ms);
		mode = gm_test_indicator();
		if (gm_test_is_measuring()) {
			if (pulsed) {
				pulse_off_until = now_ms + GM_PULSE_LED_MS;
			}
			last_mode = mode;
			if ((int32_t)(pulse_off_until - now_ms) > 0) {
				gpio_led_set(0);
			} else {
				gpio_led_set(1);
			}
			continue;
		}
		if (mode != last_mode) {
			last_mode = mode;
			last_toggle_ms = now_ms;
			fault_step = 0;
			gpio_led_set(mode == HV_LED_ON || mode == HV_LED_FAULT);
			continue;
		}
		if (mode == HV_LED_ON) {
			gpio_led_set(1);
			continue;
		}
		if (mode == HV_LED_FAST) {
			if ((now_ms - last_toggle_ms) >= 100) {
				last_toggle_ms = now_ms;
				gpio_led_toggle();
			}
			continue;
		}
		if (mode == HV_LED_SLOW) {
			if ((now_ms - last_toggle_ms) >= 500) {
				last_toggle_ms = now_ms;
				gpio_led_toggle();
			}
			continue;
		}
		if (mode == HV_LED_FAULT) {
			led_fault(now_ms, &last_toggle_ms, &fault_step);
			continue;
		}
		gpio_led_set(0);
	}
}
