#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/* Highest code a 10-bit conversion can return. */
#define ADC_COUNTS_MAX 1023UL

/*
 * Datasheet divisor. code = Vin * 1024 / Vref, so Vin = code * Vref / 1024.
 * 1023 is the maximum code, not this divisor. Phase 5C used 1024.
 */
#define ADC_FULL_SCALE_COUNTS (ADC_COUNTS_MAX + 1UL)

/* Samples averaged by adc_read_hv_raw(). Call that function from main only. */
#define ADC_HV_AVG_SAMPLES 16U

/*
 * Internal bandgap, nominal 1.1 V. Not a measured calibration.
 * The ATmega328PB bandgap is not specified as exactly 1.100 V.
 */
#define ADC_VREF_NOMINAL_UV 1100000UL

/* HV divider is 40 MOhm / 100 kOhm. The ratio 401:1 is fixed. */
#define HV_R_HIGH_OHM  40000000UL
#define HV_R_LOW_OHM   100000UL
#define HV_DIVIDER_NUM 401UL

/*
 * Temporary bring-up reference, about 1.095 V. Not a final calibration.
 * The closed-loop count conversion divides by ADC_MAX, 1023.
 */
#define ADC_VREF_MV 1095UL
#define ADC_MAX     1023UL

void adc_init(void);

/*
 * Average ADC_HV_AVG_SAMPLES conversions on ADC0.
 * The return value is that integer mean, still a raw ADC count.
 */
uint16_t adc_read_hv_raw(void);

/* Last single conversion from the most recent adc_read_hv_raw() burst. */
uint16_t adc_hv_last_raw(void);

uint32_t adc_raw_to_uv(uint16_t raw, uint32_t vref_uv);

uint32_t hv_adc_uv_to_hv_mv(uint32_t adc_uv,
			    uint32_t r_high_ohm,
			    uint32_t r_low_ohm);

/* HV-node microvolts in one expression. Nominal inputs only. */
uint32_t hv_adc_to_uv(uint16_t raw, uint32_t vref_uv);

/* Closed-loop conversion. Divider 401:1, temporary 1095 mV reference. */
uint32_t hv_adc_to_mv(uint16_t adc);
uint16_t hv_mv_to_adc(uint32_t hv_mv);

#endif
