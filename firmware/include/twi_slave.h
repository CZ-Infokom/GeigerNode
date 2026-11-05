#ifndef TWI_SLAVE_H
#define TWI_SLAVE_H

#include <stdint.h>

#define TWI_SLAVE_ADDRESS 0x66

/* ClimateGuard RadSens map. */
#define TWI_REG_DEVICE_ID        0x00
#define TWI_REG_DEVICE_ID_VALUE  0x7D
#define TWI_REG_FW_VERSION       0x01
#define TWI_REG_FW_VERSION_VALUE 3U
#define TWI_REG_DYNAMIC_RAD      0x03
#define TWI_REG_STATIC_RAD       0x06
#define TWI_REG_PULSE_COUNT      0x09
#define TWI_REG_HV_STATUS        0x0B
#define TWI_REG_LP_MODE          0x0C
#define TWI_REG_ADDRESS          0x10
#define TWI_REG_HV_ENABLE        0x11
#define TWI_REG_SENSITIVITY      0x12
#define TWI_REG_LED              0x14
#define TWI_REG_COUNT            21U

struct twi_write_cmd {
	uint8_t ptr;
	uint8_t len;
	uint8_t data[2];
};

void twi_slave_init(void);

/*
 * Hardware TWI0 stays off. A START on PC4 is polled so the address ACK
 * meets Flipper and Meshtastic, and SCL is never driven. The listen
 * address is a RAM value so a host write to 0x10 can move it.
 *
 * Radiation values are 24-bit big-endian, 0.1 uR/h. The pulse counter
 * is not in the published bank: transmitting 0x09 or 0x0A snapshots the
 * live count and clears it. A 0x0A that follows 0x09 in the same burst
 * returns the low byte of that snapshot.
 *
 * The first data byte of a master write is the register pointer. Up to
 * two following data bytes are captured. Main applies them. A read with
 * no pointer byte starts at 0x00.
 */
void twi_slave_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10);
void twi_slave_count_pulses(uint16_t count);
void twi_slave_clear_pulses(void);
void twi_slave_set_address(uint8_t address);
void twi_slave_set_sensitivity(uint16_t sensitivity);
void twi_slave_set_hv_command(uint8_t on);
void twi_slave_set_hv_status(uint8_t status);
void twi_slave_set_led(uint8_t on);
void twi_slave_set_lp(uint8_t on);

/* Returns 1 when a write with at least one data byte is waiting. */
uint8_t twi_slave_take_write(struct twi_write_cmd *out);

#endif
