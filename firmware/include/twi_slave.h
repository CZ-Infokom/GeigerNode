#ifndef TWI_SLAVE_H
#define TWI_SLAVE_H

#define TWI_SLAVE_ADDRESS 0x66

/* RadSens device ID. */
#define TWI_REG_DEVICE_ID       0x00
#define TWI_REG_DEVICE_ID_VALUE 0x7D

/*
 * Static radiation, big-endian, unit 0.1 uR/h.
 * 0x001234 is the fixed synthetic value for this bring-up.
 */
#define TWI_REG_STATIC_RAD 0x06
#define TWI_STATIC_RAD_VALUE 0x001234UL

void twi_slave_init(void);

#endif
