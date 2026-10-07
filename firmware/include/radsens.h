#ifndef RADSENS_H
#define RADSENS_H

#include <stdint.h>

/* Low nibble of register 0x0B. The high nibble is attempts already used. */
#define RADSENS_HV_FOLLOWING 0U
#define RADSENS_HV_COOLDOWN  1U
#define RADSENS_HV_GIVEN_UP  2U

void radsens_init(void);
void radsens_poll(void);
void radsens_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10);
void radsens_on_fault(void);
void radsens_set_hv_status(uint8_t state, uint8_t attempts);
uint8_t radsens_hv_command(void);
uint8_t radsens_led_enabled(void);
uint16_t radsens_sensitivity(void);

#endif
