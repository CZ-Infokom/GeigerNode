#ifndef BOARD_H
#define BOARD_H

/*
 * GeigerNode, ATmega328PB-AU, 3.3 V.
 *
 * HV_SW is PB1 / OC1A. It drives the high-voltage converter transistor.
 * gpio_init drives it low before any timer starts. Timer1 then emits the
 * open-loop pulse. Fault and reset disconnect OC1A and leave the pin low.
 *
 * LED is PD7.
 * GM_PULSE is PD2 / INT0, active low, falling edge. The pin is an input with a pull-up.
 * PD3 is spare and stays untouched.
 * HV_ADC is PC0 / ADC0. The default image leaves the converter off while it is read.
 * I2C slave address is 0x66. SDA is PC4 and SCL is PC5. Hardware TWI0
 * stays off so it cannot hold SCL. A START on SDA is handled by pin
 * change, then the transaction is polled. SCL is never driven.
 * Timer2 is the millisecond tick. Its pin outputs stay disconnected.
 * Timer1 is the HV pulse. Its hard maximum width is 100 us.
 * Every reset waits 5 s, then regulates 380 V at 2400 Hz and publishes
 * RadSens registers. There is no arm word. Pulse width stays at or below
 * 100 us. A fault latches PB1 low. During RUN the LED is off and flashes
 * on for each GM pulse.
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

#define I2C_SDA_DDR       DDRC
#define I2C_SDA_PORT      PORTC
#define I2C_SDA_PIN       PINC
#define I2C_SDA_BIT       PC4
#define I2C_SCL_DDR       DDRC
#define I2C_SCL_PORT      PORTC
#define I2C_SCL_PIN       PINC
#define I2C_SCL_BIT       PC5

#endif
