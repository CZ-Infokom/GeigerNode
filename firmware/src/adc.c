#include <avr/io.h>
#include <stdint.h>

#include "adc.h"

/* ADCSRB bit 3, missing from iom328pb.h in toolchain 4.0.0. */
#define ADC_MUX5_BIT 3

static uint16_t adc_last_raw;

static uint16_t adc_convert(void)
{
	ADCSRA |= _BV(ADSC);
	while (ADCSRA & _BV(ADSC)) {
	}
	return ADC;
}

void adc_init(void)
{
	/* ADC0 input, no pull-up. The divider provides the only DC path. */
	DDRC &= ~_BV(DDC0);
	PORTC &= ~_BV(PORTC0);
	DIDR0 |= _BV(ADC0D);

	/*
	 * Internal 1.1 V reference, ADC0.
	 * ADCSRB bit 3 is MUX5 on the ATmega328PB. This toolchain header
	 * does not define that name. Leave it clear so the mux stays on ADC0.
	 */
	ADCSRB &= ~_BV(ADC_MUX5_BIT);
	ADMUX = _BV(REFS1) | _BV(REFS0);

	/* 8 MHz / 64 = 125 kHz ADC clock. */
	ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1);

	/* First conversions also give the bandgap reference time to rise. */
	(void)adc_convert();
	(void)adc_convert();
}

uint16_t adc_read_hv_raw(void)
{
	uint32_t sum = 0;
	uint8_t n;

	for (n = 0; n < ADC_HV_AVG_SAMPLES; n++) {
		adc_last_raw = adc_convert();
		sum += adc_last_raw;
	}
	return (uint16_t)(sum / ADC_HV_AVG_SAMPLES);
}

uint16_t adc_hv_last_raw(void)
{
	return adc_last_raw;
}

uint32_t adc_raw_to_uv(uint16_t raw, uint32_t vref_uv)
{
	return (uint32_t)(((uint64_t)raw * (uint64_t)vref_uv) / ADC_FULL_SCALE_COUNTS);
}

uint32_t hv_adc_uv_to_hv_mv(uint32_t adc_uv,
			    uint32_t r_high_ohm,
			    uint32_t r_low_ohm)
{
	uint64_t numerator;

	if (r_low_ohm == 0) {
		return 0;
	}
	numerator = (uint64_t)adc_uv * ((uint64_t)r_high_ohm + (uint64_t)r_low_ohm);
	return (uint32_t)(numerator / ((uint64_t)r_low_ohm * 1000ULL));
}

uint32_t hv_adc_to_mv(uint16_t adc)
{
	return (uint32_t)(((uint64_t)adc * HV_DIVIDER_NUM * ADC_VREF_MV +
			   (ADC_MAX / 2UL)) /
			  ADC_MAX);
}

uint16_t hv_mv_to_adc(uint32_t hv_mv)
{
	uint64_t denom = (uint64_t)HV_DIVIDER_NUM * ADC_VREF_MV;
	uint64_t counts = ((uint64_t)hv_mv * ADC_MAX + (denom / 2ULL)) / denom;

	if (counts > ADC_MAX) {
		return (uint16_t)ADC_MAX;
	}
	return (uint16_t)counts;
}

uint32_t hv_adc_to_uv(uint16_t raw, uint32_t vref_uv)
{
	uint64_t numerator;

	numerator = (uint64_t)raw * (uint64_t)vref_uv;
	numerator *= (uint64_t)HV_R_HIGH_OHM + (uint64_t)HV_R_LOW_OHM;
	return (uint32_t)(numerator /
		((uint64_t)HV_R_LOW_OHM * ADC_FULL_SCALE_COUNTS));
}
