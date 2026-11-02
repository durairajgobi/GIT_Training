/*
 * Bit-level I2C slave model: a 24C02-style 256-byte EEPROM with an
 * 8-bit word address, decoded purely from SCL/SDA transitions (wired-AND
 * open-drain bus). Also exposes a message-level ep_i2c_bus_ops_t mock
 * for fast tests that do not need bit-banging.
 */
#ifndef SIM_I2C_EEPROM_H
#define SIM_I2C_EEPROM_H

#include "ep/ep_i2c.h"

enum { SIM_PIN_SCL = 10, SIM_PIN_SDA = 11 };

typedef struct {
    uint8_t mem[256];
    uint8_t ptr;
    uint8_t addr7;
    bool m_scl, m_sda; /* master drive (1 = released) */
    bool s_sda;        /* slave drive  (1 = released) */
    bool stuck_low;    /* fault injection: slave holds SDA low */
    int st;
    uint8_t shift;
    int bits;
    bool first_data, reading, master_ack;
    unsigned starts, stops;
} sim_i2c_eeprom_t;

void sim_i2c_eeprom_init(sim_i2c_eeprom_t *e, uint8_t addr7);
ep_gpio_t sim_i2c_eeprom_gpio(sim_i2c_eeprom_t *e);

/* Message-level model of the same EEPROM (ctx = sim_i2c_eeprom_t*). */
extern const ep_i2c_bus_ops_t sim_i2c_msg_ops;

#endif
