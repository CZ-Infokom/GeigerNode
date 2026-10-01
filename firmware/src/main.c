#include <avr/interrupt.h>
#include <avr/io.h>

#include "board.h"

#if F_CPU != 8000000UL
#error "This clock output is calculated for F_CPU=8000000UL"
#endif

/*
 * Timer2 compare interrupt at 1 kHz. PD7 toggles each interrupt, so the
 * pin is a 500 Hz square wave when the CPU clock is 8 MHz.
 * 8000000 / (64 * 125) = 1000 interrupts/s.
 * Timer1 stays off, so OC1A is not connected to PB1.
 * Timer2 compare outputs stay disconnected, so PB3 and PD3 are not driven.
 */
ISR(TIMER2_COMPA_vect)
{
	LED_PORT ^= _BV(LED_PORT_BIT);
}

int main(void)
{
	/* Latch HV_SW low, then drive it as an output. HV stays off. */
	HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
	HV_SW_DDR |= _BV(HV_SW_DDR_BIT);

	LED_DDR |= _BV(LED_DDR_BIT);

	OCR2A = 124;
	TCCR2A = _BV(WGM21);
	TCCR2B = _BV(CS22);
	TIMSK2 = _BV(OCIE2A);
	sei();

	for (;;) {
	}
}
