#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/atomic.h>

#include "board.h"
#include "gm_counter.h"

static volatile uint16_t gm_pulse_count;

ISR(INT0_vect)
{
	gm_pulse_count++;
}

void gm_counter_init(void)
{
	/* Input with pull-up. A short to ground is a falling edge. */
	GM_PULSE_DDR &= ~_BV(GM_PULSE_DDR_BIT);
	GM_PULSE_PORT |= _BV(GM_PULSE_PORT_BIT);

	EICRA = (EICRA & ~(_BV(ISC01) | _BV(ISC00))) | _BV(ISC01);
	EIFR = _BV(INTF0);
	EIMSK |= _BV(INT0);
}

uint16_t gm_counter_count(void)
{
	uint16_t count;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		count = gm_pulse_count;
	}
	return count;
}
