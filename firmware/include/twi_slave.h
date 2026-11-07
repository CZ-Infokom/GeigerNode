#ifndef TWI_SLAVE_H
#define TWI_SLAVE_H

#include <stdint.h>

#ifdef GEIGERNODE_PROTOCOL_NATIVE
#include "gm_counter.h"
#include "gn_protocol.h"

#define TWI_SLAVE_ADDRESS GN_I2C_ADDRESS
#define TWI_REG_COUNT     GN_REG_MAP_SIZE
#define TWI_WRITE_MAX     4U

#else

#define TWI_SLAVE_ADDRESS 0x66

/* ClimateGuard RadSens map. */
#define TWI_REG_DEVICE_ID        0x00
#define TWI_REG_DEVICE_ID_VALUE  0x7D
#define TWI_REG_FW_VERSION       0x01
#define TWI_REG_FW_VERSION_VALUE 4U
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
#define TWI_WRITE_MAX            2U

#endif

struct twi_write_cmd {
	uint8_t ptr;
	uint8_t len;
	uint8_t data[TWI_WRITE_MAX];
};

void twi_slave_init(void);

/*
 * Hardware TWI0 slave at PC4/PC5. Clock stretching is bounded to one
 * ISR. A read with no pointer byte starts at 0x00. 0xA0 does not read
 * TWDR0. TWSTO is written only for a bus error.
 *
 * Two register banks. A publish writes the idle bank and swings
 * regs_pub. A read freezes that published bank pointer for the
 * transaction so 0x04..0x38 (native) or 0x03..0x08 (RadSens) cannot
 * tear, and a concurrent publish updates the other bank.
 *
 * Native protocol (0x67): the published bank is protocol v2. Reads do
 * not clear counters. Main applies a 4-byte write at 0x12, a 2-byte
 * write at 0x1E or 0x20, or a 1-byte write of 0x01 at 0x38. Any other
 * write is acknowledged and ignored.
 *
 * RadSens (0x66): a host write to 0x10 updates TWAR0 for the next
 * transaction. Radiation values are 24-bit big-endian, 0.1 uR/h. The
 * pulse counter is not in the bank: transmitting 0x09 or 0x0A
 * snapshots the live count and clears it. Up to two data bytes after
 * the pointer are applied in main.
 */
uint8_t twi_slave_take_write(struct twi_write_cmd *out);

#ifdef GEIGERNODE_PROTOCOL_NATIVE

void twi_slave_publish_native(uint32_t dose_nsvh, uint32_t cpm_x10,
			      uint16_t status, uint32_t sequence,
			      uint32_t cal_q16, uint32_t count,
			      uint32_t elapsed_ms,
			      const struct gm_histogram *histogram);
void twi_slave_set_native_status(uint16_t status);
void twi_slave_set_native_config(uint16_t window_s, uint16_t holdoff_us,
				 uint32_t cal_q16);
void twi_slave_clear_native_histogram(void);

#else

void twi_slave_publish_radiation(uint32_t dynamic_x10, uint32_t static_x10);
void twi_slave_count_pulses(uint16_t count);
void twi_slave_clear_pulses(void);
void twi_slave_set_address(uint8_t address);
void twi_slave_set_sensitivity(uint16_t sensitivity);
void twi_slave_set_hv_command(uint8_t on);
void twi_slave_set_hv_status(uint8_t status);
void twi_slave_set_led(uint8_t on);
void twi_slave_set_lp(uint8_t on);

#endif

#endif
