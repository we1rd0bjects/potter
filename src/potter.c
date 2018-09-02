#include <inttypes.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include "../lib/avr-i2c-slave/I2CSlave.h"

#define I2C_ADDRESS (0x10)
#define POTS_LEN (8)
#define PWM_LEN (3)
#define BUTTONS_LEN (8) /* expected to be not greater than 8 */
#define BUTTONS_DEBOUNCE (200) /* that gives around 0.1 sec */

static volatile uint8_t pots[POTS_LEN];
static volatile uint8_t pwm_frequency[PWM_LEN];
static volatile uint8_t pwm_duty[PWM_LEN];
static volatile uint8_t buttons_state;
static volatile uint8_t buttons_falling_edges;
static volatile uint8_t buttons_rising_edges;

static volatile uint8_t i2c_state;
static volatile uint8_t i2c_reg;
static volatile uint8_t i2c_write_data;

static void pwm_set_frequency(uint8_t channel, uint8_t f) {
    if ( (channel >= PWM_LEN)
            || (f > 7)
            || (channel > 0 && (f == 3 || f == 5)) ) {
        return;
    }
    if (channel == 0) {
        pwm_frequency[0] = f;
    } else {
        pwm_frequency[1] = f;
        pwm_frequency[2] = f;
    }

    uint8_t fixed_f = f;
    switch (channel) {
        case 0:
            TCCR2 = ( (TCCR2 & 0xf8) | f );
            if (f > 0) {
                TCCR2 |= (1 << COM21);
            } else {
                TCCR2 &= ~(1 << COM21);
            }
            break;
        case 1:
        case 2:
            if (f > 4) {
                fixed_f -= 2;
            } else if (f > 2) {
                fixed_f -= 1;
            }
            TCCR1B = ( (TCCR1B & 0xf8) | fixed_f );
            if (f > 0) {
                TCCR1A |= ((1 << COM1A1) | (1 << COM1B1));
            } else {
                TCCR1A &= ~((1 << COM1A1) | (1 << COM1B1));
            }
            break;
    }
}

static void pwm_set_duty(uint8_t channel, uint8_t duty) {
    pwm_duty[channel] = duty;
}

static void buttons_poll(void) {
    static uint8_t debounce_timers[BUTTONS_LEN];
    uint8_t hw_port_state = PIND;

    for (uint8_t i = 0; i < BUTTONS_LEN; ++i) {
        if (debounce_timers[i] > 0) {
            --debounce_timers[i];
            continue;
        }

        uint8_t hw_state = ((hw_port_state >> i) & 0x01);
        uint8_t known_state = ((buttons_state >> i) & 0x01);

        if (hw_state != known_state) {
            debounce_timers[i] = BUTTONS_DEBOUNCE;
            buttons_state ^= (1 << i);
            if (hw_state) {
                buttons_rising_edges |= (1 << i);
            } else {
                buttons_falling_edges |= (1 << i);
            }
        }
    }
}

static void pwm_integrate(void) {
    uint8_t pwm_current[PWM_LEN] = { OCR2, OCR1AL, OCR1BL };
    uint8_t pwm_next[PWM_LEN];

    for (uint8_t i = 0; i < PWM_LEN; ++i) {
        uint8_t current = pwm_current[i];
        uint8_t target = pwm_duty[i];
        uint8_t next;

        if (target < current) {
            next = current - 1;
        } else if (target > current) {
            next = current + 1;
        } else {
            next = current;
        }

        pwm_next[i] = next;
    }

    OCR2 = pwm_next[0];
    OCR1AL = pwm_next[1];
    OCR1BL = pwm_next[2];
}

ISR(TIMER0_OVF_vect) {
    static uint8_t prescale = 0;
    prescale = !prescale;
    if (prescale) {
        return;
    }
    buttons_poll();
    pwm_integrate();
}

ISR(ADC_vect) {
    uint8_t channel = ADMUX & 0x0f;
    uint8_t next_channel = (channel + 1) & 0x0f;
    pots[channel] = ADCH;
    ADMUX = ((ADMUX & 0xf0) | next_channel);
    ADCSRA |= (1 << ADSC);
}

static void i2c_write_register() {
    if ( (i2c_reg >= 0x40) && (i2c_reg <= 0x45) ) {
        uint8_t masked = (i2c_reg & 0x0f);
        uint8_t channel = (masked >> 1);
        uint8_t method = (masked & 0x01);

        if (method == 0) {
            pwm_set_frequency(channel, i2c_write_data);
        } else if (method == 1) {
            pwm_set_duty(channel, i2c_write_data);
        }
    } else if (i2c_reg == 0x60) {
        PORTB = i2c_write_data;
    }
}

static void I2C_received(uint8_t received_data) {
    switch (i2c_state) {
        case 0:
            i2c_reg = received_data;
            i2c_state = 1;
            break;

        case 1:
            i2c_write_data = received_data;
            i2c_write_register();
            i2c_state = 0;
            break;
    }
}

static void I2C_requested() {
    uint8_t result;

    if (i2c_reg < 0x08) {
        result = pots[i2c_reg];
    } else if (i2c_reg == 0x20) {
        result = buttons_state;
    } else if (i2c_reg == 0x21) {
        result = buttons_falling_edges;
        buttons_falling_edges = 0;
    } else if (i2c_reg == 0x22) {
        result = buttons_rising_edges;
        buttons_rising_edges = 0;
    } else {
        result = 0x00;
    }

    i2c_state = 0;
    I2C_transmitByte(result);
}

static void init(void) {
    DDRB = 0xff;
    PORTD = 0xff;

    ADMUX = (1 << ADLAR)
          | (1 << REFS0);
    ADCSRA = (1 << ADEN)
           | (1 << ADIE)
           | (1 << ADSC)
           | (1 << ADPS2)
           | (1 << ADPS1);

    TCCR0 = (1 << CS01);
    TCCR1A = (1 << WGM10);
    TCCR1B = (1 << WGM12);
    TCCR2 = (1 << WGM21)
          | (1 << WGM20);
    TIMSK |= (1 << TOIE0);

    I2C_setCallbacks(I2C_received, I2C_requested);
    I2C_init(I2C_ADDRESS);

    sei();
}

int main(void) {
    init();

    while (1) {
        sleep_mode();
    }
}
