/*
 * ltc3562.c - Portable driver for the LTC3562 I2C quad step-down regulator.
 * See ltc3562.h for the protocol description.
 */
#include "ltc3562.h"

static const uint8_t sel_bit[LTC3562_NUM_REGULATORS] = {
    [LTC3562_R600A] = LTC3562_SEL_R600A,
    [LTC3562_R400A] = LTC3562_SEL_R400A,
    [LTC3562_R600B] = LTC3562_SEL_R600B,
    [LTC3562_R400B] = LTC3562_SEL_R400B,
};

static const char *const reg_names[LTC3562_NUM_REGULATORS] = {
    [LTC3562_R600A] = "R600A",
    [LTC3562_R400A] = "R400A",
    [LTC3562_R600B] = "R600B",
    [LTC3562_R400B] = "R400B",
};

static bool reg_valid(ltc3562_reg_t reg)
{
    return (unsigned)reg < LTC3562_NUM_REGULATORS;
}

static bool mode_valid(ltc3562_mode_t mode)
{
    return (unsigned)mode <= LTC3562_MODE_MASK;
}

static uint8_t dac_max(ltc3562_reg_t reg)
{
    return ltc3562_is_type_a(reg) ? LTC3562_DAC_A_MASK : LTC3562_DAC_B_MASK;
}

bool ltc3562_is_type_a(ltc3562_reg_t reg)
{
    return reg == LTC3562_R600A || reg == LTC3562_R400A;
}

const char *ltc3562_reg_name(ltc3562_reg_t reg)
{
    return reg_valid(reg) ? reg_names[reg] : "?";
}

/* DAC code -> regulated voltage at the FB pin (Type A) or VOUT (Type B). */
static uint32_t dac_to_dac_mv(ltc3562_reg_t reg, uint8_t dac)
{
    uint32_t base = ltc3562_is_type_a(reg) ? LTC3562_TYPE_A_FB_MIN_MV
                                           : LTC3562_TYPE_B_OUT_MIN_MV;
    return base + (uint32_t)dac * LTC3562_STEP_MV;
}

/* Output voltage in mV, or 0 if a Type A divider is not configured. */
uint32_t ltc3562_dac_to_mv(const ltc3562_dev_t *dev, ltc3562_reg_t reg,
                           uint8_t dac)
{
    uint32_t vdac;

    if (!dev || !reg_valid(reg) || dac > dac_max(reg))
        return 0;

    vdac = dac_to_dac_mv(reg, dac);
    if (!ltc3562_is_type_a(reg))
        return vdac;

    {
        const ltc3562_channel_t *c = &dev->ch[reg];
        uint64_t num;

        if (c->r_bot == 0)
            return 0;
        /* Vout = Vfb * (r_top + r_bot) / r_bot, rounded to nearest mV. */
        num = (uint64_t)vdac * ((uint64_t)c->r_top + c->r_bot);
        return (uint32_t)((num + c->r_bot / 2) / c->r_bot);
    }
}

/*
 * Find the DAC code whose output is closest to `mv`.  Fails if the request
 * lies more than half a step beyond either end of the achievable range.
 */
static int mv_to_dac(const ltc3562_dev_t *dev, ltc3562_reg_t reg,
                     uint32_t mv, uint8_t *dac)
{
    uint8_t max = dac_max(reg);
    uint32_t lo = ltc3562_dac_to_mv(dev, reg, 0);
    uint32_t hi = ltc3562_dac_to_mv(dev, reg, max);
    uint32_t half_step;
    uint32_t best_err = UINT32_MAX;
    uint8_t code;

    if (lo == 0)
        return LTC3562_ERR_CONFIG;

    half_step = (ltc3562_dac_to_mv(dev, reg, 1) - lo) / 2;
    if (mv + half_step < lo || mv > hi + half_step)
        return LTC3562_ERR_RANGE;

    for (code = 0; code <= max; code++) {
        uint32_t out = ltc3562_dac_to_mv(dev, reg, code);
        uint32_t err = out > mv ? out - mv : mv - out;

        if (err < best_err) {
            best_err = err;
            *dac = code;
        }
    }
    return LTC3562_OK;
}

static int send(ltc3562_dev_t *dev, uint8_t sub, uint8_t data)
{
    uint8_t buf[2] = { sub, data };

    return dev->write(dev->user, dev->addr, buf, sizeof(buf)) ? LTC3562_ERR_IO
                                                              : LTC3562_OK;
}

/* Write a full command for one regulator and update the shadow on success. */
static int commit(ltc3562_dev_t *dev, ltc3562_reg_t reg, uint8_t dac,
                  ltc3562_mode_t mode, bool on)
{
    uint8_t sub = sel_bit[reg] | ((uint8_t)mode & LTC3562_MODE_MASK);
    uint8_t data = (on ? LTC3562_DATA_ENABLE : 0u) | (dac & dac_max(reg));
    int ret = send(dev, sub, data);

    if (ret == LTC3562_OK) {
        dev->ch[reg].dac = dac;
        dev->ch[reg].mode = mode;
        dev->ch[reg].enabled = on;
    }
    return ret;
}

int ltc3562_init(ltc3562_dev_t *dev, ltc3562_i2c_write_fn write, void *user,
                 uint8_t addr)
{
    int i;

    if (!dev || !write)
        return LTC3562_ERR_PARAM;

    dev->write = write;
    dev->user = user;
    dev->addr = addr ? addr : LTC3562_I2C_ADDR;
    for (i = 0; i < LTC3562_NUM_REGULATORS; i++) {
        dev->ch[i].enabled = false;
        dev->ch[i].mode = LTC3562_MODE_PULSE_SKIP;
        dev->ch[i].dac = 0;
        dev->ch[i].r_top = 0;
        dev->ch[i].r_bot = 0;
    }
    return ltc3562_shutdown_all(dev);
}

int ltc3562_set_divider(ltc3562_dev_t *dev, ltc3562_reg_t reg,
                        uint32_t r_top, uint32_t r_bot)
{
    if (!dev || !reg_valid(reg) || !ltc3562_is_type_a(reg) || r_bot == 0)
        return LTC3562_ERR_PARAM;

    dev->ch[reg].r_top = r_top;
    dev->ch[reg].r_bot = r_bot;
    return LTC3562_OK;
}

int ltc3562_write_raw(ltc3562_dev_t *dev, ltc3562_reg_t reg, uint8_t dac,
                      ltc3562_mode_t mode, bool on)
{
    if (!dev || !reg_valid(reg) || !mode_valid(mode))
        return LTC3562_ERR_PARAM;
    if (dac > dac_max(reg))
        return LTC3562_ERR_RANGE;

    return commit(dev, reg, dac, mode, on);
}

int ltc3562_configure(ltc3562_dev_t *dev, ltc3562_reg_t reg, uint32_t mv,
                      ltc3562_mode_t mode, bool on, uint32_t *actual_mv)
{
    uint8_t dac = 0;
    int ret;

    if (!dev || !reg_valid(reg) || !mode_valid(mode))
        return LTC3562_ERR_PARAM;

    ret = mv_to_dac(dev, reg, mv, &dac);
    if (ret != LTC3562_OK)
        return ret;

    ret = commit(dev, reg, dac, mode, on);
    if (ret == LTC3562_OK && actual_mv)
        *actual_mv = ltc3562_dac_to_mv(dev, reg, dac);
    return ret;
}

int ltc3562_set_voltage_mv(ltc3562_dev_t *dev, ltc3562_reg_t reg,
                           uint32_t mv, uint32_t *actual_mv)
{
    if (!dev || !reg_valid(reg))
        return LTC3562_ERR_PARAM;

    return ltc3562_configure(dev, reg, mv, dev->ch[reg].mode,
                             dev->ch[reg].enabled, actual_mv);
}

int ltc3562_get_voltage_mv(const ltc3562_dev_t *dev, ltc3562_reg_t reg,
                           uint32_t *mv)
{
    uint32_t v;

    if (!dev || !reg_valid(reg) || !mv)
        return LTC3562_ERR_PARAM;

    v = ltc3562_dac_to_mv(dev, reg, dev->ch[reg].dac);
    if (v == 0)
        return LTC3562_ERR_CONFIG;
    *mv = v;
    return LTC3562_OK;
}

int ltc3562_set_mode(ltc3562_dev_t *dev, ltc3562_reg_t reg,
                     ltc3562_mode_t mode)
{
    if (!dev || !reg_valid(reg))
        return LTC3562_ERR_PARAM;

    return ltc3562_write_raw(dev, reg, dev->ch[reg].dac, mode,
                             dev->ch[reg].enabled);
}

int ltc3562_enable(ltc3562_dev_t *dev, ltc3562_reg_t reg, bool on)
{
    if (!dev || !reg_valid(reg))
        return LTC3562_ERR_PARAM;

    return commit(dev, reg, dev->ch[reg].dac, dev->ch[reg].mode, on);
}

int ltc3562_shutdown_all(ltc3562_dev_t *dev)
{
    int ret, i;

    if (!dev || !dev->write)
        return LTC3562_ERR_PARAM;

    /* Broadcast: every regulator gets enable=0, DAC=0, pulse-skip mode. */
    ret = send(dev, LTC3562_SEL_ALL | LTC3562_MODE_PULSE_SKIP, 0x00);
    if (ret != LTC3562_OK)
        return ret;

    for (i = 0; i < LTC3562_NUM_REGULATORS; i++) {
        dev->ch[i].enabled = false;
        dev->ch[i].mode = LTC3562_MODE_PULSE_SKIP;
        dev->ch[i].dac = 0;
    }
    return LTC3562_OK;
}
