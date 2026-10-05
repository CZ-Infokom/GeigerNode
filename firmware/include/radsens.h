#ifndef RADSENS_H
#define RADSENS_H

#include <stdint.h>

void radsens_init(void);
void radsens_publish(uint32_t dynamic_x10,
		     uint32_t static_x10,
		     uint16_t pulses,
		     uint8_t hv_on);

#endif
