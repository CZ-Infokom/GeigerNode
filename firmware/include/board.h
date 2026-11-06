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
 * I2C SDA is PC4 and SCL is PC5. Hardware TWI0 is the slave.
 * PROTOCOL=native answers at 0x67. PROTOCOL=radsens answers at 0x66.
 * Pin-change bit-bang is off. The bit-banged image is on
 * branch fallback/bitbang-i2c.
 * Timer2 is the millisecond tick. Its pin outputs stay disconnected.
 * Timer1 is the HV pulse. Its hard maximum width is 100 us.
 * Every reset waits 500 ms, then regulates 380 V at 2400 Hz. There is
 * no arm word. The native map is read-only. A RadSens host can stop or
 * start the generator and can store a new address and sensitivity.
 * Pulse width stays at or below 100 us. A fault forces PB1 low. An
 * operational fault may start again after a cooldown; an illegal pulse
 * or rate, or a spent budget, stays off until a RadSens host writes 1
 * or the native image is reset. During the 500 ms wait the LED is on.
 * During RUN it flashes on for each GM pulse.
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
