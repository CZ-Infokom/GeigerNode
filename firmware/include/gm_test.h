#ifndef GM_TEST_H
#define GM_TEST_H

#include <stdint.h>

#include "gm_counter.h"

/*
 * One SI-29BG record at EEPROM address 256. The frequency-series image
 * used address 0. This test does not write that record.
 *
 * Layout, little-endian, 106 bytes:
 *   magic, version, target_hv_v, frequency_hz,
 *   total_pulses, measurement_pulses, measured_cpm, min_interval_us,
 *   bin_lt_50us, bin_50_100us, bin_100_200us, bin_200_500us, bin_500us_1ms,
 *   bin_1_2ms, bin_2_5ms, bin_5_10ms, bin_ge_10ms,
 *   close_intervals_us[16], close_interval_count,
 *   min_cps, max_cps,
 *   final_adc, min_adc, max_adc,
 *   final_hv_v, min_hv_v, max_hv_v,
 *   final_pulse_us, min_pulse_us, max_pulse_us,
 *   ms_at_100us, status, state, checksum
 *
 * Version 4 is the same 10-minute window. It keeps the exclusive 1 us
 * bins and also stores the first 16 gaps below 500 us, in the order they
 * arrived. close_interval_count is that stored length, 0 through 16.
 * measured_cpm is the integer counts per minute, measurement_pulses / 10,
 * when that window finishes. It is UINT32_MAX when the window did not
 * finish. Dose is not computed. Sensitivity stays unused. Blanking is 0.
 *
 * Each interval increments exactly one bin. The bins and the close log
 * do not reject pulses. A few close edges stay GM_TEST_OK.
 * GM_TEST_EXCESSIVE_FAST_PULSES is a flag on a finished window, not a
 * reason the firmware stopped early. min_interval_us is UINT32_MAX when
 * the window has no pair.
 */

#define GM_TEST_MAGIC               0x474DU
#define GM_TEST_VERSION             4U
#define GM_TEST_EEPROM_ADDR         256U

#define GM_STATE_ARMED              0xA541U
#define GM_STATE_RUNNING            0xB652U
#define GM_STATE_COMPLETE           0xC763U
#define GM_STATE_ABORTED            0xD874U

#define GM_TEST_OK                      1U
#define GM_TEST_NO_PULSES               2U
#define GM_TEST_EXCESSIVE_FAST_PULSES   3U
#define GM_TEST_HV_FAULT                4U
#define GM_TEST_HV_TIMEOUT              5U
#define GM_TEST_OVERVOLTAGE             6U
#define GM_TEST_ABORTED                 7U

#define GM_PRE_HV_MS                5000UL
#define GM_HV_SETTLE_HOLD_MS        1000UL
#define GM_HV_SETTLE_TIMEOUT_MS     20000UL
#define GM_MEASURE_MS               600000UL
#define GM_MEASURE_BUCKETS          (GM_MEASURE_MS / 1000UL)

/* 5 V at 401:1, 1095 mV reference, divisor 1023. About +/-5 V around 380 V. */
#define GM_HV_SETTLE_COUNTS         12U

/*
 * Suspicious, not a shutdown. Fewer close edges than this stay GM_TEST_OK
 * so a couple of detector echoes are visible in the counters only.
 */
#define GM_SUSPECT_LT_1MS           25UL
#define GM_SUSPECT_MIN_PULSES       30UL

#define GM_CPM_UNSET                0xFFFFFFFFUL

struct gm_test_result {
	uint16_t magic;
	uint16_t version;
	uint16_t target_hv_v;
	uint16_t frequency_hz;
	uint32_t total_pulses;
	uint32_t measurement_pulses;
	uint32_t measured_cpm;
	uint32_t min_interval_us;
	uint16_t bin_lt_50us;
	uint16_t bin_50_100us;
	uint16_t bin_100_200us;
	uint16_t bin_200_500us;
	uint16_t bin_500us_1ms;
	uint16_t bin_1_2ms;
	uint16_t bin_2_5ms;
	uint16_t bin_5_10ms;
	uint16_t bin_ge_10ms;
	uint16_t close_intervals_us[GM_CLOSE_LOG];
	uint16_t close_interval_count;
	uint16_t min_cps;
	uint16_t max_cps;
	uint16_t final_adc;
	uint16_t min_adc;
	uint16_t max_adc;
	uint16_t final_hv_v;
	uint16_t min_hv_v;
	uint16_t max_hv_v;
	uint16_t final_pulse_us;
	uint16_t min_pulse_us;
	uint16_t max_pulse_us;
	uint16_t ms_at_100us;
	uint16_t status;
	uint16_t state;
	uint16_t checksum;
} __attribute__((packed));

static inline uint16_t gm_test_checksum(const struct gm_test_result *record)
{
	const uint8_t *raw = (const uint8_t *)record;
	uint16_t nbytes = (uint16_t)(sizeof *record - 2U);
	uint16_t sum = 0;
	uint16_t i;

	for (i = 0; i < nbytes; i = (uint16_t)(i + 2U)) {
		uint16_t word = (uint16_t)((uint16_t)raw[i] | ((uint16_t)raw[i + 1] << 8));

		sum = (uint16_t)(sum + word);
	}
	return sum;
}

/* reset_flags is the MCUSR value captured and cleared at the start of main. */
void gm_test_boot(uint8_t reset_flags);
void gm_test_poll(uint32_t now_ms);
uint8_t gm_test_indicator(void);
uint8_t gm_test_is_measuring(void);

#endif
