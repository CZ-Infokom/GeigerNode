#ifndef RADSENS_H
#define RADSENS_H

#include <stdint.h>

void radsens_init(void);
void radsens_poll(void);
void radsens_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10);
void radsens_on_fault(void);
uint8_t radsens_hv_command(void);
uint8_t radsens_led_enabled(void);
uint16_t radsens_sensitivity(void);

#endif
