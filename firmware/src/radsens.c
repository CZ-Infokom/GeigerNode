#include <stdint.h>

#include "radsens.h"
#include "twi_slave.h"

static uint32_t static_radiation_x10;

void radsens_set_static_urh_x10(uint32_t value)
{
	static_radiation_x10 = value & 0x00FFFFFFUL;
	twi_slave_set_static_radiation(static_radiation_x10);
}

void radsens_init(void)
{
	radsens_set_static_urh_x10(TWI_STATIC_RAD_VALUE);
}
