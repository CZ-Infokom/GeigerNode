#include <avr/io.h>

#include "board.h"
#include "gpio.h"

void gpio_init(void)
{
	/* Latch HV_SW low, then drive it as an output. HV stays off. */
	HV_SW_PORT &= ~_BV(HV_SW_PORT_BIT);
	HV_SW_DDR |= _BV(HV_SW_DDR_BIT);

	LED_DDR |= _BV(LED_DDR_BIT);
}

void gpio_led_toggle(void)
{
	LED_PORT ^= _BV(LED_PORT_BIT);
}
