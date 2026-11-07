#ifndef GN_PROTOCOL_H
#define GN_PROTOCOL_H

#include <stdint.h>

/*
 * Native GeigerNode I2C protocol v1. Slave address 0x67.
 * Map: firmware/geigernode-i2c-protocol.md
 */

#define GN_I2C_ADDRESS           0x67U
#define GN_PROTOCOL_VERSION      2U
#define GN_DEVICE_ID             0x474EU
#define GN_CAPS_DOSE             0x01U
#define GN_CAPS_RAW              0x02U
#define GN_CAPS_CONFIG           0x04U
#define GN_CAPS_HISTOGRAM        0x08U
#define GN_CAPS_VALUE \
	(GN_CAPS_DOSE | GN_CAPS_RAW | GN_CAPS_CONFIG | GN_CAPS_HISTOGRAM)
#define GN_REG_MAP_SIZE          0x39U

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
#define GN_REG_WINDOW_S          0x1EU
#define GN_REG_HOLDOFF_US        0x20U
#define GN_REG_MIN_INTERVAL      0x22U
#define GN_REG_BIN_LT_50US       0x26U
#define GN_REG_BIN_50_100US      0x28U
#define GN_REG_BIN_100_200US     0x2AU
#define GN_REG_BIN_200_500US     0x2CU
#define GN_REG_BIN_500US_1MS     0x2EU
#define GN_REG_BIN_1_2MS         0x30U
#define GN_REG_BIN_2_5MS         0x32U
#define GN_REG_BIN_5_10MS        0x34U
#define GN_REG_BIN_GE_10MS       0x36U
#define GN_REG_HISTOGRAM_FLAGS   0x38U

#define GN_HISTOGRAM_SATURATED   0x01U

/* Native ring ceiling. The default published window stays five minutes. */
#define GN_WINDOW_MIN_S          1U
#define GN_WINDOW_MAX_S          400U
#define GN_WINDOW_DEFAULT_S      300U
#define GN_HOLDOFF_MAX_US        300U
#define GN_HOLDOFF_DEFAULT_US    300U

#define GN_STATUS_VALID          0x0001U
#define GN_STATUS_WINDOW_FULL    0x0002U
#define GN_STATUS_HV_FOLLOWING   0x0004U
#define GN_STATUS_HV_SETTLING    0x0008U
#define GN_STATUS_HV_COOLDOWN    0x0010U
#define GN_STATUS_HV_FAULT       0x0020U
#define GN_STATUS_OVERFLOW       0x0040U

#endif
