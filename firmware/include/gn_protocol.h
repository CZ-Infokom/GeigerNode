#ifndef GN_PROTOCOL_H
#define GN_PROTOCOL_H

#include <stdint.h>

/*
 * Native GeigerNode I2C protocol v1. Slave address 0x67.
 * Map: firmware/geigernode-i2c-protocol.md
 */

#define GN_I2C_ADDRESS           0x67U
#define GN_PROTOCOL_VERSION      1U
#define GN_DEVICE_ID             0x474EU
#define GN_CAPS_VALUE            0x03U
#define GN_REG_MAP_SIZE          0x1EU

#define GN_REG_DEVICE_ID         0x00U
#define GN_REG_PROTOCOL          0x02U
#define GN_REG_CAPS              0x03U
#define GN_REG_DOSE              0x04U
#define GN_REG_CPM_X10           0x08U
#define GN_REG_STATUS            0x0CU
#define GN_REG_SEQUENCE          0x0EU
#define GN_REG_CAL               0x12U
#define GN_REG_COUNT             0x16U
#define GN_REG_ELAPSED           0x1AU

#define GN_STATUS_VALID          0x0001U
#define GN_STATUS_WINDOW_FULL    0x0002U
#define GN_STATUS_HV_FOLLOWING   0x0004U
#define GN_STATUS_HV_SETTLING    0x0008U
#define GN_STATUS_HV_COOLDOWN    0x0010U
#define GN_STATUS_HV_FAULT       0x0020U
#define GN_STATUS_OVERFLOW       0x0040U

#endif
