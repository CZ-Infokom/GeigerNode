#ifndef GM_COUNTER_H
#define GM_COUNTER_H

#include <stdint.h>

/*
 * Tube profile for a later swap to the SBM-20.
 * sensitivity_imp_per_ur is provisional. Zero means "do not use".
 * Pulse counting must not read it and must not scale counts by it.
 */
struct gm_tube_profile {
	uint16_t nominal_hv_v;
	uint16_t sensitivity_imp_per_ur;
};

#define GM_TUBE_SI29BG_HV_V                 380U
#define GM_TUBE_SI29BG_SENSITIVITY_UNUSED   0U

extern const struct gm_tube_profile gm_tube_si29bg;

/* Timer2 millisecond tick. The ISR that owns it only increments this. */
extern volatile uint32_t system_ms;

/*
 * Exclusive inter-arrival bins. One gap increments exactly one bin.
 * Edges are still counted when the gap is short. Nothing is rejected.
 *
 * The first GM_CLOSE_LOG gaps below GM_CLOSE_LIMIT_US are stored in
 * order. Later close gaps stay in the bins only. GM_BLANKING_US is 0:
 * raw and accepted counts are the same until a later image splits them.
 */
#define GM_CLOSE_LOG        16U
#define GM_CLOSE_LIMIT_US   500U
#define GM_BLANKING_US      0U

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

void gm_counter_init(void);
uint16_t gm_tube_nominal_hv_v(void);

/* Measurement window. Opening clears window counts and interval stats. */
void gm_counter_open_window(void);
void gm_counter_close_window(void);
uint32_t gm_counter_window(void);
void gm_counter_snapshot(struct gm_counter_snapshot *out);

/* Returns 1 once after the ISR sees a falling edge, then clears the flag. */
uint8_t gm_counter_take_pulse(void);

#endif
