#ifndef TWI_SLAVE_H
#define TWI_SLAVE_H

#include <stdint.h>

#define TWI_SLAVE_ADDRESS 0x66

/* ClimateGuard RadSens map, first bring-up subset. */
#define TWI_REG_DEVICE_ID        0x00
#define TWI_REG_DEVICE_ID_VALUE  0x7D
#define TWI_REG_FW_VERSION       0x01
#define TWI_REG_FW_VERSION_VALUE 1U
#define TWI_REG_DYNAMIC_RAD      0x03
#define TWI_REG_STATIC_RAD       0x06
#define TWI_REG_PULSE_COUNT      0x09
#define TWI_REG_ADDRESS          0x10
#define TWI_REG_HV_ENABLE        0x11
#define TWI_REG_SENSITIVITY      0x12
#define TWI_REG_LED              0x14
#define TWI_REG_COUNT            21U

void twi_slave_init(void);

/*
 * One atomic snapshot for every live field a sequential read can cross.
 * Radiation values are 24-bit big-endian, 0.1 uR/h. Pulse count is 16-bit
 * big-endian and is not cleared by a read. The first data byte of a master
 * write is the register pointer. The next read burst starts there. A read
 * with no pointer byte starts at 0x00 (device ID 0x7D). Bytes inside one
 * read burst increment. Writes after the pointer byte are ignored, so an
 * I2C write cannot start or stop HV.
 */
void twi_slave_publish(uint32_t dynamic_x10,
		       uint32_t static_x10,
		       uint16_t pulses,
		       uint8_t hv_on);

#endif
