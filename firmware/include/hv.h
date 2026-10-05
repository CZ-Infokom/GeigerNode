#ifndef HV_H
#define HV_H

#include <stdint.h>

/*
 * SI-29BG bring-up. The regulator target is 380 V at 2400 Hz.
 * Pulse cap is 100 us. The hard abort is 932 counts, around 400 V.
 * No controller state may start a higher target, a higher rate, or a
 * wider pulse. A fault latches PB1 low. There is no automatic restart.
 *
 * The one-shot characterization tables remain in this file.
 * This image does not run them. The 2500 Hz production hold is closed.
 *
 * The tables below are the last characterization sweep. It ran with a
 * 995-count ceiling. This image does not run that sweep, and its abort
 * is the 932-count production limit.
 *
 * Test 0: 400 V at 2500 Hz, 932 counts
 * Test 1: 400 V at 2600 Hz, 932 counts
 * Test 2: 400 V at 2700 Hz, 932 counts
 * Test 3: 400 V at 2800 Hz, 932 counts
 * Test 4: 400 V at 2900 Hz, 932 counts
 * Test 5: 400 V at 3000 Hz, 932 counts
 *
 * Each test runs 10 s. HV is off for 10 s between tests.
 * Saturation is recorded and the ladder continues.
 * A hard abort or other fault stops the rest.
 * Blank EEPROM, or an explicit ARMED word, runs the series once.
 * RUNNING, COMPLETE, and ABORTED do not start HV.
 */

#define HV_MAX_PULSE_US         100U
#define HV_START_PULSE_US       10U
#define HV_FREQ_SERIES_MAX_HZ   3000U
#define HV_TEST_MAX_RUNTIME_MS  10000U
#define HV_SERIES_OFF_MS        10000U
#define HV_SERIES_ARM_DELAY_MS  5000U
#define HV_CTRL_PERIOD_MS       20U

#define HV_FREQ_TEST_0_VOLTS    400U
#define HV_FREQ_TEST_0_HZ       2500U
#define HV_FREQ_TEST_1_VOLTS    400U
#define HV_FREQ_TEST_1_HZ       2600U
#define HV_FREQ_TEST_2_VOLTS    400U
#define HV_FREQ_TEST_2_HZ       2700U
#define HV_FREQ_TEST_3_VOLTS    400U
#define HV_FREQ_TEST_3_HZ       2800U
#define HV_FREQ_TEST_4_VOLTS    400U
#define HV_FREQ_TEST_4_HZ       2900U
#define HV_FREQ_TEST_5_VOLTS    400U
#define HV_FREQ_TEST_5_HZ       3000U

#define HV_BENCH_VOLTS          380U
#define HV_BENCH_HZ             2500U

/* SI-29BG operating point. These are hard limits for this image. */
#define HV_TARGET_V             380U
#define HV_SWITCH_FREQ_HZ       2400U

#define HV_HYST_COUNTS          5U
#define HV_FAST_LOW_COUNTS      20U
#define HV_FAST_HIGH_COUNTS     15U
#define HV_OVERSHOOT_COUNTS     25U
#define HV_ADC_HARD_ABORT       932U
#define HV_OK_BAND_COUNTS       20U
#define HV_SAT_TICKS            50U
#define HV_RISE_COUNTS          8U

#define HV_OFF                  0U
#define HV_STARTING             1U
#define HV_REGULATING           2U
#define HV_COMPLETE             3U
#define HV_FAULT                4U

#define HV_TEST_OK              1U
#define HV_TEST_TIMEOUT         2U
#define HV_TEST_SATURATED       3U
#define HV_TEST_OVERVOLTAGE     4U
#define HV_TEST_FAULT           5U

#define HV_FLAG_SATURATED       0x0001U
#define HV_FLAG_OVERSHOOT       0x0002U
#define HV_FLAG_RISING          0x0004U

#define HV_FREQ_MAGIC           0x4653U
#define HV_FREQ_VERSION         8U
#define HV_FREQ_TEST_COUNT      6U
#define HV_FREQ_STATE_ARMED     0xA541U
#define HV_FREQ_STATE_RUNNING   0xB652U
#define HV_FREQ_STATE_COMPLETE  0xC763U
#define HV_FREQ_STATE_ABORTED   0xD874U

#define HV_LED_OFF              0U
#define HV_LED_ON               1U
#define HV_LED_FAST             2U
#define HV_LED_SLOW             3U
#define HV_LED_FAULT            4U

struct hv_freq_result {
	uint16_t target_volts;
	uint16_t target_adc;
	uint16_t frequency_hz;
	uint16_t final_adc;
	uint16_t max_adc;
	uint16_t min_adc_settled;
	uint16_t final_hv_volts;
	uint16_t max_hv_volts;
	uint16_t final_pulse_us;
	uint16_t max_pulse_us;
	uint16_t min_pulse_settled_us;
	uint16_t control_updates_at_cap;
	uint16_t milliseconds_at_cap;
	uint16_t status;
} __attribute__((packed));

struct hv_freq_series {
	uint16_t magic;
	uint16_t version;
	uint16_t series_state;
	uint16_t completed_tests;
	struct hv_freq_result result[HV_FREQ_TEST_COUNT];
	uint16_t checksum;
} __attribute__((packed));

static inline uint16_t hv_freq_checksum(const struct hv_freq_series *series)
{
	const uint8_t *raw = (const uint8_t *)series;
	uint16_t nbytes = (uint16_t)(sizeof *series - 2U);
	uint16_t sum = 0;
	uint16_t i;

	for (i = 0; i < nbytes; i = (uint16_t)(i + 2U)) {
		uint16_t word = (uint16_t)((uint16_t)raw[i] | ((uint16_t)raw[i + 1] << 8));

		sum = (uint16_t)(sum + word);
	}
	return sum;
}

void hv_series_boot(void);
void hv_series_poll(uint16_t now_ms);
uint8_t hv_series_indicator(void);

void hv_bench_boot(void);
uint8_t hv_bench_start(uint16_t now_ms);
void hv_bench_poll(uint16_t now_ms);
uint8_t hv_bench_indicator(void);

/* SI-29BG test. Begins only at HV_TARGET_V and HV_SWITCH_FREQ_HZ. */
#define HV_SETTLE_COUNTS      12U
#define HV_SETTLE_HOLD_MS     1000UL
#define HV_SETTLE_TIMEOUT_MS  20000UL

uint8_t hv_gm_begin(uint16_t now_ms);
void hv_gm_poll(uint16_t now_ms);
void hv_gm_stop(void);
uint8_t hv_is_starting(void);
uint8_t hv_is_regulating(void);
uint8_t hv_is_fault(void);
uint8_t hv_switch_is_off(void);
uint16_t hv_latched_pulse_us(void);
uint16_t hv_latched_status(void);
uint32_t hv_cap_milliseconds(void);

extern volatile uint16_t adc_raw;
extern volatile uint16_t adc_avg;
extern volatile uint16_t adc_max;
extern volatile uint16_t target_adc;
extern volatile uint16_t pulse_width_us;
extern volatile uint16_t max_pulse_width_us;
extern volatile uint16_t active_frequency_hz;
extern volatile uint32_t estimated_hv_mv;
extern volatile uint32_t max_estimated_hv_mv;
extern volatile uint8_t regulator_state;
extern volatile uint16_t status_flags;

#endif
