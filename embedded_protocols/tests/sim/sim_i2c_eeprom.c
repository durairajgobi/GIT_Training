#include "sim_i2c_eeprom.h"

#include <string.h>

enum { S_IDLE, S_ADDR, S_ADDR_ACK, S_RX, S_RX_ACK, S_TX, S_TX_ACK };

static bool line_sda(const sim_i2c_eeprom_t *e)
{
    return e->m_sda && e->s_sda && !e->stuck_low;
}

static void on_scl_rising(sim_i2c_eeprom_t *e)
{
    switch (e->st) {
    case S_ADDR:
    case S_RX:
        e->shift = (uint8_t)((e->shift << 1) | (line_sda(e) ? 1 : 0));
        e->bits++;
        break;
    case S_TX_ACK:
        e->master_ack = !line_sda(e);
        break;
    default:
        break;
    }
}

static void on_scl_falling(sim_i2c_eeprom_t *e)
{
    switch (e->st) {
    case S_ADDR:
        if (e->bits == 8) {
            if ((e->shift >> 1) == e->addr7) {
                e->reading = e->shift & 1;
                e->s_sda = false; /* ACK */
                e->st = S_ADDR_ACK;
            } else {
                e->st = S_IDLE; /* not for us */
            }
        }
        break;
    case S_ADDR_ACK:
        e->s_sda = true;
        e->bits = 0;
        if (e->reading) {
            e->shift = e->mem[e->ptr++];
            e->s_sda = (e->shift >> 7) & 1;
            e->st = S_TX;
        } else {
            e->shift = 0;
            e->first_data = true;
            e->st = S_RX;
        }
        break;
    case S_RX:
        if (e->bits == 8) {
            if (e->first_data) {
                e->ptr = e->shift;
                e->first_data = false;
            } else {
                e->mem[e->ptr++] = e->shift;
            }
            e->s_sda = false; /* ACK */
            e->st = S_RX_ACK;
        }
        break;
    case S_RX_ACK:
        e->s_sda = true;
        e->bits = 0;
        e->shift = 0;
        e->st = S_RX;
        break;
    case S_TX:
        e->bits++;
        if (e->bits < 8) {
            e->s_sda = (e->shift >> (7 - e->bits)) & 1;
        } else {
            e->s_sda = true; /* release for master ACK/NACK */
            e->st = S_TX_ACK;
        }
        break;
    case S_TX_ACK:
        if (e->master_ack) {
            e->shift = e->mem[e->ptr++];
            e->bits = 0;
            e->s_sda = (e->shift >> 7) & 1;
            e->st = S_TX;
        } else {
            e->st = S_IDLE; /* NACK: wait for STOP */
        }
        break;
    default:
        break;
    }
}

static void gpio_write(void *ctx, uint32_t pin, bool level)
{
    sim_i2c_eeprom_t *e = ctx;
    bool old_scl = e->m_scl;
    bool old_sda = line_sda(e);

    if (pin == SIM_PIN_SCL)
        e->m_scl = level;
    else if (pin == SIM_PIN_SDA)
        e->m_sda = level;
    else
        return;

    if (pin == SIM_PIN_SDA) {
        bool new_sda = line_sda(e);
        if (e->m_scl && old_scl && old_sda != new_sda) {
            if (!new_sda) { /* START / repeated START */
                e->starts++;
                e->st = S_ADDR;
                e->bits = 0;
                e->shift = 0;
                e->s_sda = true;
            } else { /* STOP */
                e->stops++;
                e->st = S_IDLE;
                e->s_sda = true;
            }
        }
        return;
    }
    if (!old_scl && level)
        on_scl_rising(e);
    else if (old_scl && !level)
        on_scl_falling(e);
}

static bool gpio_read(void *ctx, uint32_t pin)
{
    sim_i2c_eeprom_t *e = ctx;
    if (pin == SIM_PIN_SCL)
        return e->m_scl;
    if (pin == SIM_PIN_SDA)
        return line_sda(e);
    return true;
}

void sim_i2c_eeprom_init(sim_i2c_eeprom_t *e, uint8_t addr7)
{
    memset(e, 0, sizeof(*e));
    e->addr7 = addr7;
    e->m_scl = e->m_sda = e->s_sda = true;
    for (int i = 0; i < 256; i++)
        e->mem[i] = (uint8_t)(0xFF - i);
}

ep_gpio_t sim_i2c_eeprom_gpio(sim_i2c_eeprom_t *e)
{
    ep_gpio_t g = {gpio_write, gpio_read, e};
    return g;
}

/* ------------------------- message-level --------------------------- */

static ep_status_t msg_xfer(void *ctx, ep_i2c_msg_t *msgs, size_t n)
{
    sim_i2c_eeprom_t *e = ctx;
    for (size_t i = 0; i < n; i++) {
        ep_i2c_msg_t *m = &msgs[i];
        if (m->addr != e->addr7)
            return EP_ERR_NACK;
        if (m->flags & EP_I2C_M_RD) {
            for (uint16_t k = 0; k < m->len; k++)
                m->buf[k] = e->mem[e->ptr++];
        } else {
            for (uint16_t k = 0; k < m->len; k++) {
                if (k == 0)
                    e->ptr = m->buf[0];
                else
                    e->mem[e->ptr++] = m->buf[k];
            }
        }
    }
    return EP_OK;
}

const ep_i2c_bus_ops_t sim_i2c_msg_ops = {.xfer = msg_xfer};
