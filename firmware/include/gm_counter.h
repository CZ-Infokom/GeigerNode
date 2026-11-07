#ifndef GM_COUNTER_H
#define GM_COUNTER_H

#include <stdint.h>

#include "radiation.h"

/*
 * Tube profiles. Sensitivity is stored x10 so 137.5 impulses/uR is exact.
 * gm_tube_active is the fitted tube. The SBM-20 datasheet recommends
 * 400 V; this board stays at 380 V, inside the 350-475 V range.
 * Pulse counting must not read a profile and must not scale counts by it.
 */
struct gm_tube_profile {
	uint16_t nominal_hv_v;
	uint16_t sensitivity_imp_per_ur_x10;
};

#define GM_TUBE_SI29BG_HV_V 380U
#define GM_TUBE_SBM20_HV_V  380U

extern const struct gm_tube_profile gm_tube_si29bg;
extern const struct gm_tube_profile gm_tube_sbm20;
extern const struct gm_tube_profile *const gm_tube_active;

/* Timer2 millisecond tick. The ISR that owns it only increments this. */
extern volatile uint32_t system_ms;

/*
 * Exclusive inter-arrival bins. One accepted gap increments exactly one bin.
 * The first GM_CLOSE_LOG accepted gaps below GM_CLOSE_LIMIT_US are stored
 * in order. Later close gaps stay in the bins only.
 *
 * After an accepted edge, further falling edges are ignored for the
 * live holdoff. An ignored edge does not move that stamp. GM_BLANKING_US
 * is the default and the ceiling: 300 us covers the SBM-20 190 us dead
 * time and the 151-185 us recovery seen on the earlier tube. Bins and
 * counts record accepted edges only.
 */
#define GM_CLOSE_LOG        16U
#define GM_CLOSE_LIMIT_US   500U
#define GM_BLANKING_US      300U

struct gm_counter_snapshot {
	uint32_t total_count;
	uint32_t window_count;
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
	uint8_t close_interval_count;
};

/*
 * Inter-arrival histogram for the native I2C map. Nine exclusive bins in
 * the same order as gm_counter_snapshot. flags bit 0 is set when any bin
 * is at 65535. The close log stays off the bus.
 */
#define GM_HISTOGRAM_BIN_COUNT 9U
#define GM_HISTOGRAM_SATURATED 0x01U

struct gm_histogram {
	uint32_t min_interval_us;
	uint16_t bins[GM_HISTOGRAM_BIN_COUNT];
	uint8_t flags;
};

void gm_counter_init(void);

/* 0 accepts every edge. Values above GM_BLANKING_US clamp to that ceiling. */
void gm_counter_set_blanking_us(uint16_t blanking_us);
uint16_t gm_tube_nominal_hv_v(void);

/* Whole-number impulses/uR from the active profile, rounded to nearest. */
uint16_t gm_tube_sensitivity_reg(void);

/* Measurement window. Opening clears window counts and interval stats. */
void gm_counter_open_window(void);
void gm_counter_close_window(void);
uint32_t gm_counter_window(void);
uint32_t gm_counter_total(void);
void gm_counter_snapshot(struct gm_counter_snapshot *out);
void gm_counter_histogram(struct gm_histogram *out);

/* Returns 1 once after the ISR sees a falling edge, then clears the flag. */
uint8_t gm_counter_take_pulse(void);

#endif
