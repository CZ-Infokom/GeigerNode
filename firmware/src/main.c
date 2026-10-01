#include <avr/io.h>
#include <util/delay.h>

#include "board.h"

int main(void)
{
	/* Latch HV_SW low, then drive it as an output. HV stays off. */
	HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
	HV_SW_DDR |= _BV(HV_SW_DDR_BIT);

	LED_DDR |= _BV(LED_DDR_BIT);

	for (;;) {
		LED_PORT ^= _BV(LED_PORT_BIT);
		_delay_ms(500);
	}
}
