#include <stdint.h>

#include "radsens.h"
#include "twi_slave.h"

void radsens_init(void)
{
	radsens_publish(0, 0, 0, 0);
}

void radsens_publish(uint32_t dynamic_x10,
		     uint32_t static_x10,
		     uint16_t pulses,
		     uint8_t hv_on)
{
	twi_slave_publish(dynamic_x10, static_x10, pulses, hv_on);
}
