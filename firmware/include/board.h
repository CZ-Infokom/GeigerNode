#ifndef BOARD_H
#define BOARD_H

/*
 * GeigerNode, ATmega328PB-AU, 3.3 V.
 *
 * HV_SW is PB1 / OC1A. It drives the high-voltage converter transistor.
 * Firmware holds that pin low. Timer1 stays disabled, so OC1A stays disconnected.
 *
 * LED is PD7.
 * GM_PULSE is PD2 / INT0, active low, falling edge. The pin is an input with a pull-up.
 * PD3 is spare and stays untouched.
 * HV_ADC is PC0 / ADC0. The converter stays off while it is read.
 * TWI0 slave address is 0x66. SDA is PC4 and SCL is PC5.
 * Timer2 is the millisecond tick. Its pin outputs stay disconnected.
 * Timer1 stays disabled.
 */

#define HV_SW_PORT     PORTB
#define HV_SW_DDR      DDRB
#define HV_SW_PORT_BIT PORTB1
#define HV_SW_DDR_BIT  DDB1

#define LED_PORT       PORTD
#define LED_DDR        DDRD
#define LED_PORT_BIT   PORTD7
#define LED_DDR_BIT    DDD7

#define GM_PULSE_DDR      DDRD
#define GM_PULSE_PORT     PORTD
#define GM_PULSE_DDR_BIT  DDD2
#define GM_PULSE_PORT_BIT PORTD2

#endif
