#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/* 10-bit conversion: full-scale raw 1024 corresponds to Vref. */
#define ADC_FULL_SCALE_COUNTS 1024U

/* Internal reference, nominal 1.1 V. No measured calibration yet. */
#define ADC_VREF_NOMINAL_UV  1100000UL
#define ADC_VREF_MEASURED_UV ADC_VREF_NOMINAL_UV

/* HV node to HV_ADC divider. 400 V is about 1.0 V at ADC0. */
#define HV_DIVIDER_HIGH_OHM 40000000UL
#define HV_DIVIDER_LOW_OHM  100000UL

void adc_init(void);
uint16_t adc_read_hv_raw(void);
uint32_t adc_raw_to_uv(uint16_t raw, uint32_t vref_uv);
uint32_t hv_adc_to_uv(uint16_t raw, uint32_t vref_uv);

#endif
