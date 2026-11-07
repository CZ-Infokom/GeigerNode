#ifndef GN_CONFIG_H
#define GN_CONFIG_H

#include <stdint.h>

#include "twi_slave.h"

/*
 * Native protocol v2 settings. The RadSens image does not call these.
 * Window, holdoff, and calibration survive reset in EEPROM. A blank or
 * bad record uses the defaults. make flash chip-erases EEPROM. Write 1
 * at 0x38, or an accepted holdoff write, clears the histogram.
 */

void gn_config_init(void);
void gn_config_on_write(const struct twi_write_cmd *cmd);
uint16_t gn_config_window_s(void);
uint16_t gn_config_holdoff_us(void);
uint32_t gn_config_cal_q16(void);

#endif
