#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>

#include "gm_counter.h"
#include "gpio.h"

#if F_CPU != 8000000UL
#error "The 1 ms tick is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 compare interrupt at 1 kHz: 8000000 / (64 * 125).
 * The tick ISR only counts. Main toggles PD7 every 500 ms and also
 * once for each new GM pulse.
 * Timer1 stays off. Timer2 compare outputs stay disconnected.
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

int main(void)
{
	uint16_t last_toggle_ms = 0;
	uint16_t last_pulses = 0;

	gpio_init();
	gm_counter_init();
	timer2_start_1ms();
	sei();

	for (;;) {
		uint16_t now_ms;
		uint16_t pulses;

		cli();
		now_ms = milliseconds;
		sei();

		pulses = gm_counter_count();
		if (pulses != last_pulses) {
			last_pulses = pulses;
			gpio_led_toggle();
		}

		if ((uint16_t)(now_ms - last_toggle_ms) >= 500) {
			last_toggle_ms = now_ms;
			gpio_led_toggle();
		}
	}
}
