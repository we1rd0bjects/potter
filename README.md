

# Potter

Exposes an AVR atmega8's IO capabilities as an I2C slave. It has:
 * 8 analog inputs (8-bit ADC); only 6 with PDIP
 * 3 analog outputs (8-bit PWM)
 * 8 digital inputs
 * 5 (+3) digital outputs

Changes to PWM duty cycles are low-pass filtered; a 0x00 -> 0xff transition
needs ~0.13 sec. The PWM outputs can also be used as simple digital outputs.

Digital inputs are debounced (~0.1 sec). The internal pull-up resistors are
enabled.

Initial state:
 * all digital outputs are low
 * all PWM outputs are stopped and low

AVR pin configuration:
 * Analog inputs: PC0-PC7 (ADC0-ADC7)
 * Analog outputs: PB1-PB3 (OC1A, OC1B, OC2)
 * Digital outputs: PB0, PB4-PB7 (+PB1-PB3)
 * Digital inputs: PD0-PD7

Needful things:
 * build: ```make clean && make all```
 * flash: ```make program```
 * fuses: ```-U lfuse:w:0xe4:m -U hfuse:w:0xd9:m```


# I2C map

The default device address is 0x10. You can change it by modifying
the I2C_ADDRESS constant in src/potter.c.

The registers are as follows:

| Address       | R/W   | Description                                   |
| ------------- | ----- | --------------------------------------------- |
| 0x00 - 0x07   | R     | Analog inputs                                 |
| 0x20          | R     | Digital inputs, current state                 |
| 0x21          | R     | Digital inputs, rising edges since last read  |
| 0x22          | R     | Digital inputs, falling edges since last read |
| 0x40          | W     | Analog / PWM output 0 frequency               |
| 0x41          | W     | Analog / PWM output 0 duty cycle              |
| 0x42          | W     | Analog / PWM outputs 1 & 2 frequency          |
| 0x43          | W     | Analog / PWM output 1 duty cycle              |
| 0x44          | W     | Analog / PWM outputs 1 & 2 frequency          |
| 0x45          | W     | Analog / PWM output 2 duty cycle              |
| 0x60          | W     | Bit 0 (LSB): digital output 0<br>Bits 1-3: PWM0-2 output states when stopped<br>Bits 4-7: digital outputs 1..4 |

Analog / PWM output frequency values:

| Value         | Frequency     | Notes                                 |
| ------------- | ------------- | ------------------------------------- |
| 0x00          | stopped       | output is indefinite (high Z)         |
| 0x01          | 31250 Hz      | -                                     |
| 0x02          | 3906 Hz       | -                                     |
| 0x03          | 976 Hz        | not supported on PWM1, PWM2           |
| 0x04          | 488 Hz        | -                                     |
| 0x05          | 244 Hz        | not supported on PWM1, PWM2           |
| 0x06          | 122 Hz        | -                                     |
| 0x07          | 30 Hz         | -                                     |


# Implementation notes

MCU peripherals setup:
 * ADC prescaling: /64; ADC input clock = 125KHz
 * PWM0 = Timer2, PWM1&PWM2 = Timer1
 * Timer0 is used as a periodic "tick" interrupt with clk/8 prescale
   (~3906 tick / second; that is software prescaled by another 2 (~1953 tick))

I2C/TWI slave library comes from here: https://github.com/thegouger/avr-i2c-slave;
thank you!

# TODO

 * make the pwm "lpf" configurable
 * externalize I2C_ADDRESS to a build variable
 * clean up the code, it's plain disgusting
