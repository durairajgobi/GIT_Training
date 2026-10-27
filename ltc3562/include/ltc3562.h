/*
 * ltc3562.h - Portable driver for the Analog Devices / Linear Technology
 *             LTC3562 I2C quad step-down regulator.
 *
 * The LTC3562 contains four synchronous buck regulators:
 *
 *   R600A  600 mA  Type A  external divider, I2C-set feedback (425..800 mV)
 *   R400A  400 mA  Type A  external divider, I2C-set feedback (425..800 mV)
 *   R600B  600 mA  Type B  internal divider, I2C-set output   (600..3775 mV)
 *   R400B  400 mA  Type B  internal divider, I2C-set output   (600..3775 mV)
 *
 * I2C protocol (write-only, 7-bit slave address 0x65):
 *
 *   [S] [0x65 << 1 | W] [sub-address] [data] [P]
 *
 *   sub-address  bit 7  program R600A
 *                bit 6  program R400A
 *                bit 5  program R600B
 *                bit 4  program R400B
 *                bit 1:0 operating mode (applies to every selected regulator)
 *   data         bit 7  enable
 *                bit 6:0 DAC code (Type A uses bits 3:0, Type B bits 6:0)
 *
 * Because the part cannot be read back, the driver keeps a shadow copy of
 * each regulator's enable / mode / DAC state and always writes a complete
 * command for the regulator being changed.
 *
 * The driver has no platform dependencies: the caller supplies an I2C write
 * callback (see ltc3562_i2c_write_fn).
 */
#ifndef LTC3562_H
#define LTC3562_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7-bit I2C slave address (fixed). */
#define LTC3562_I2C_ADDR            0x65u

/* Sub-address byte fields. */
#define LTC3562_SEL_R600A           0x80u
#define LTC3562_SEL_R400A           0x40u
#define LTC3562_SEL_R600B           0x20u
#define LTC3562_SEL_R400B           0x10u
#define LTC3562_SEL_ALL             0xF0u
#define LTC3562_MODE_MASK           0x03u

/* Data byte fields. */
#define LTC3562_DATA_ENABLE         0x80u
#define LTC3562_DAC_A_MASK          0x0Fu
#define LTC3562_DAC_B_MASK          0x7Fu

/* DAC ranges, in millivolts. */
#define LTC3562_STEP_MV             25u
#define LTC3562_TYPE_A_FB_MIN_MV    425u
#define LTC3562_TYPE_A_FB_MAX_MV    800u
#define LTC3562_TYPE_B_OUT_MIN_MV   600u
#define LTC3562_TYPE_B_OUT_MAX_MV   3775u

/* Return codes. */
typedef enum {
    LTC3562_OK          =  0,
    LTC3562_ERR_PARAM   = -1,   /* NULL pointer / bad regulator / bad mode */
    LTC3562_ERR_RANGE   = -2,   /* requested voltage cannot be produced    */
    LTC3562_ERR_IO      = -3,   /* I2C transfer failed                     */
    LTC3562_ERR_CONFIG  = -4    /* Type A divider not configured           */
} ltc3562_status_t;

typedef enum {
    LTC3562_R600A = 0,
    LTC3562_R400A,
    LTC3562_R600B,
    LTC3562_R400B,
    LTC3562_NUM_REGULATORS
} ltc3562_reg_t;

typedef enum {
    LTC3562_MODE_PULSE_SKIP   = 0x0,  /* low ripple at light load (default) */
    LTC3562_MODE_LDO          = 0x1,  /* linear regulator, lowest noise     */
    LTC3562_MODE_FORCED_BURST = 0x2,  /* fixed-ripple burst                 */
    LTC3562_MODE_BURST        = 0x3   /* best light-load efficiency         */
} ltc3562_mode_t;

/*
 * Platform I2C write hook.  Must perform a single write transaction of `len`
 * bytes to the 7-bit address `addr`.  Return 0 on success, non-zero on error.
 */
typedef int (*ltc3562_i2c_write_fn)(void *user, uint8_t addr,
                                    const uint8_t *buf, size_t len);

/* Shadow state of one regulator. */
typedef struct {
    bool           enabled;
    ltc3562_mode_t mode;
    uint8_t        dac;       /* raw DAC code currently programmed        */
    uint32_t       r_top;     /* Type A only: FB divider top resistor (Ω) */
    uint32_t       r_bot;     /* Type A only: FB divider bottom resistor  */
} ltc3562_channel_t;

typedef struct {
    ltc3562_i2c_write_fn write;
    void                *user;
    uint8_t              addr;
    ltc3562_channel_t    ch[LTC3562_NUM_REGULATORS];
} ltc3562_dev_t;

/*
 * Initialise the driver and put all four regulators into a known state:
 * disabled, pulse-skip mode, lowest DAC code.  `addr` of 0 selects the
 * default LTC3562_I2C_ADDR.
 */
int ltc3562_init(ltc3562_dev_t *dev, ltc3562_i2c_write_fn write, void *user,
                 uint8_t addr);

/*
 * Describe the external feedback divider of a Type A regulator so the driver
 * can translate output voltages:  Vout = Vfb * (1 + r_top / r_bot).
 * Does not touch the bus.
 */
int ltc3562_set_divider(ltc3562_dev_t *dev, ltc3562_reg_t reg,
                        uint32_t r_top, uint32_t r_bot);

/*
 * Program the output voltage of a regulator, in millivolts.  The nearest
 * achievable setting is chosen; the voltage actually programmed is returned
 * through `actual_mv` (may be NULL).  The enable state and mode are kept.
 */
int ltc3562_set_voltage_mv(ltc3562_dev_t *dev, ltc3562_reg_t reg,
                           uint32_t mv, uint32_t *actual_mv);

/* Current (shadowed) output voltage in millivolts. */
int ltc3562_get_voltage_mv(const ltc3562_dev_t *dev, ltc3562_reg_t reg,
                           uint32_t *mv);

int ltc3562_set_mode(ltc3562_dev_t *dev, ltc3562_reg_t reg,
                     ltc3562_mode_t mode);

int ltc3562_enable(ltc3562_dev_t *dev, ltc3562_reg_t reg, bool on);

/*
 * Set voltage, mode and enable in a single I2C transaction - the preferred
 * way to bring up a rail.
 */
int ltc3562_configure(ltc3562_dev_t *dev, ltc3562_reg_t reg, uint32_t mv,
                      ltc3562_mode_t mode, bool on, uint32_t *actual_mv);

/* Disable all four regulators with one broadcast write. */
int ltc3562_shutdown_all(ltc3562_dev_t *dev);

/* Low-level: program a raw DAC code (range-checked for the regulator type). */
int ltc3562_write_raw(ltc3562_dev_t *dev, ltc3562_reg_t reg, uint8_t dac,
                      ltc3562_mode_t mode, bool on);

/* Conversion helpers (no bus access). */
bool     ltc3562_is_type_a(ltc3562_reg_t reg);
uint32_t ltc3562_dac_to_mv(const ltc3562_dev_t *dev, ltc3562_reg_t reg,
                           uint8_t dac);
const char *ltc3562_reg_name(ltc3562_reg_t reg);

#ifdef __cplusplus
}
#endif

#endif /* LTC3562_H */
