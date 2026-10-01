#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>

#include "adc.h"
#include "gm_counter.h"
#include "gpio.h"

#if F_CPU != 8000000UL
#error "The 1 ms tick is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 compare interrupt at 1 kHz: 8000000 / (64 * 125).
 * The ISR only counts. Main shows the HV ADC reading on PD7.
 * Timer1 stays off. The HV converter stays off.
 */
static volatile uint16_t milliseconds;

/* Above this HV-node voltage the LED stays lit. Converter must be off. */
#define HV_OFF_LIMIT_UV 10000000UL
#define ADC_BLINK_LIMIT 20U

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

static uint16_t millis_now(void)
{
	uint16_t now;

	cli();
	now = milliseconds;
	sei();
	return now;
}

static void wait_ms(uint16_t duration)
{
	uint16_t start = millis_now();

	while ((uint16_t)(millis_now() - start) < duration) {
	}
}

static void show_adc(uint16_t raw, uint32_t hv_uv)
{
	uint8_t blinks;
	uint8_t i;

	/* Long flash marks the start of the count. */
	gpio_led_set(1);
	wait_ms(300);
	gpio_led_set(0);
	wait_ms(400);

	if (hv_uv > HV_OFF_LIMIT_UV) {
		gpio_led_set(1);
		wait_ms(1500);
		gpio_led_set(0);
		wait_ms(500);
		return;
	}

	blinks = raw > ADC_BLINK_LIMIT ? ADC_BLINK_LIMIT : (uint8_t)raw;
	for (i = 0; i < blinks; i++) {
		gpio_led_set(1);
		wait_ms(80);
		gpio_led_set(0);
		wait_ms(220);
	}
	wait_ms(800);
}

int main(void)
{
	gpio_init();
	gm_counter_init();
	adc_init();
	timer2_start_1ms();
	sei();

	for (;;) {
		uint16_t raw = adc_read_hv_raw();
		uint32_t hv_uv = hv_adc_to_uv(raw, ADC_VREF_MEASURED_UV);

		show_adc(raw, hv_uv);
	}
}
