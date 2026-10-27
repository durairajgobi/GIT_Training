/*
 * test_ltc3562.c - Host unit tests for the LTC3562 driver using a mock bus.
 */
#include <stdio.h>
#include <string.h>

#include "ltc3562.h"

struct mock_bus {
    uint8_t addr;
    uint8_t last[2];
    int     writes;
    int     fail;
};

static int mock_write(void *user, uint8_t addr, const uint8_t *buf, size_t len)
{
    struct mock_bus *m = user;

    if (m->fail)
        return -1;
    if (len != 2)
        return -1;
    m->addr = addr;
    memcpy(m->last, buf, 2);
    m->writes++;
    return 0;
}

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void setup(ltc3562_dev_t *dev, struct mock_bus *m)
{
    memset(m, 0, sizeof(*m));
    CHECK(ltc3562_init(dev, mock_write, m, 0) == LTC3562_OK);
}

static void test_init_broadcasts_shutdown(void)
{
    ltc3562_dev_t dev;
    struct mock_bus m;

    setup(&dev, &m);
    CHECK(m.writes == 1);
    CHECK(m.addr == 0x65);
    CHECK(m.last[0] == 0xF0);
    CHECK(m.last[1] == 0x00);
    CHECK(ltc3562_init(NULL, mock_write, &m, 0) == LTC3562_ERR_PARAM);
    CHECK(ltc3562_init(&dev, NULL, &m, 0) == LTC3562_ERR_PARAM);
}

static void test_type_b_voltage(void)
{
    ltc3562_dev_t dev;
    struct mock_bus m;
    uint32_t mv = 0;

    setup(&dev, &m);

    /* 1800 mV -> (1800 - 600) / 25 = 48 = 0x30, burst mode, enabled. */
    CHECK(ltc3562_configure(&dev, LTC3562_R600B, 1800, LTC3562_MODE_BURST,
                            true, &mv) == LTC3562_OK);
    CHECK(mv == 1800);
    CHECK(m.last[0] == (0x20 | 0x03));
    CHECK(m.last[1] == (0x80 | 0x30));

    /* Endpoints. */
    CHECK(ltc3562_configure(&dev, LTC3562_R400B, 600, LTC3562_MODE_LDO,
                            false, &mv) == LTC3562_OK);
    CHECK(m.last[0] == 0x11 && m.last[1] == 0x00 && mv == 600);
    CHECK(ltc3562_configure(&dev, LTC3562_R400B, 3775, LTC3562_MODE_LDO,
                            false, &mv) == LTC3562_OK);
    CHECK(m.last[1] == 0x7F && mv == 3775);

    /* Rounds to nearest step: 1812 -> 1800, 1813 -> 1825. */
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600B, 1812, &mv) == 0);
    CHECK(mv == 1800);
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600B, 1813, &mv) == 0);
    CHECK(mv == 1825);
    /* set_voltage keeps mode/enable from the shadow. */
    CHECK(m.last[0] == 0x23 && (m.last[1] & 0x80));

    /* Out of range is rejected without bus traffic. */
    m.writes = 0;
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600B, 500, NULL) ==
          LTC3562_ERR_RANGE);
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600B, 3800, NULL) ==
          LTC3562_ERR_RANGE);
    CHECK(m.writes == 0);

    CHECK(ltc3562_get_voltage_mv(&dev, LTC3562_R600B, &mv) == 0);
    CHECK(mv == 1825);
}

static void test_type_a_voltage(void)
{
    ltc3562_dev_t dev;
    struct mock_bus m;
    uint32_t mv = 0;

    setup(&dev, &m);

    /* No divider yet. */
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600A, 1200, NULL) ==
          LTC3562_ERR_CONFIG);
    CHECK(ltc3562_get_voltage_mv(&dev, LTC3562_R600A, &mv) ==
          LTC3562_ERR_CONFIG);
    /* Dividers only apply to Type A. */
    CHECK(ltc3562_set_divider(&dev, LTC3562_R600B, 1, 1) == LTC3562_ERR_PARAM);
    CHECK(ltc3562_set_divider(&dev, LTC3562_R600A, 1, 0) == LTC3562_ERR_PARAM);

    /* Divider gain 3: Vout range 1275..2400 mV in 75 mV steps. */
    CHECK(ltc3562_set_divider(&dev, LTC3562_R600A, 200000, 100000) == 0);
    CHECK(ltc3562_configure(&dev, LTC3562_R600A, 1800, LTC3562_MODE_PULSE_SKIP,
                            true, &mv) == LTC3562_OK);
    /* Vfb = 600 mV -> code 7. */
    CHECK(mv == 1800);
    CHECK(m.last[0] == 0x80);
    CHECK(m.last[1] == (0x80 | 0x07));

    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600A, 2400, &mv) == 0);
    CHECK(mv == 2400 && m.last[1] == 0x8F);
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600A, 1240, &mv) == 0);
    CHECK(mv == 1275 && m.last[1] == 0x80);
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600A, 1200, NULL) ==
          LTC3562_ERR_RANGE);
    CHECK(ltc3562_set_voltage_mv(&dev, LTC3562_R600A, 2500, NULL) ==
          LTC3562_ERR_RANGE);

    /* Unity gain (FB tied to VOUT) on R400A. */
    CHECK(ltc3562_set_divider(&dev, LTC3562_R400A, 0, 1) == 0);
    CHECK(ltc3562_configure(&dev, LTC3562_R400A, 800, LTC3562_MODE_FORCED_BURST,
                            true, &mv) == 0);
    CHECK(mv == 800 && m.last[0] == 0x42 && m.last[1] == 0x8F);

    /* Raw writes are range-checked per type. */
    CHECK(ltc3562_write_raw(&dev, LTC3562_R400A, 0x10, LTC3562_MODE_LDO,
                            true) == LTC3562_ERR_RANGE);
    CHECK(ltc3562_write_raw(&dev, LTC3562_R400B, 0x7F, LTC3562_MODE_LDO,
                            true) == LTC3562_OK);
}

static void test_mode_enable_and_shadow(void)
{
    ltc3562_dev_t dev;
    struct mock_bus m;

    setup(&dev, &m);
    CHECK(ltc3562_configure(&dev, LTC3562_R400B, 3300, LTC3562_MODE_PULSE_SKIP,
                            false, NULL) == 0);
    CHECK(m.last[1] == 0x6C);

    CHECK(ltc3562_enable(&dev, LTC3562_R400B, true) == 0);
    CHECK(m.last[0] == 0x10 && m.last[1] == 0xEC);

    CHECK(ltc3562_set_mode(&dev, LTC3562_R400B, LTC3562_MODE_LDO) == 0);
    CHECK(m.last[0] == 0x11 && m.last[1] == 0xEC);
    CHECK(ltc3562_set_mode(&dev, LTC3562_R400B, (ltc3562_mode_t)4) ==
          LTC3562_ERR_PARAM);

    CHECK(ltc3562_enable(&dev, LTC3562_R400B, false) == 0);
    CHECK(m.last[0] == 0x11 && m.last[1] == 0x6C);

    /* Failed I/O must not corrupt the shadow state. */
    m.fail = 1;
    CHECK(ltc3562_enable(&dev, LTC3562_R400B, true) == LTC3562_ERR_IO);
    CHECK(dev.ch[LTC3562_R400B].enabled == false);
    CHECK(ltc3562_shutdown_all(&dev) == LTC3562_ERR_IO);
    CHECK(dev.ch[LTC3562_R400B].dac == 0x6C);
    m.fail = 0;

    CHECK(ltc3562_shutdown_all(&dev) == 0);
    CHECK(m.last[0] == 0xF0 && m.last[1] == 0x00);
    CHECK(dev.ch[LTC3562_R400B].dac == 0 &&
          dev.ch[LTC3562_R400B].mode == LTC3562_MODE_PULSE_SKIP);

    CHECK(ltc3562_enable(&dev, LTC3562_NUM_REGULATORS, true) ==
          LTC3562_ERR_PARAM);
}

int main(void)
{
    test_init_broadcasts_shutdown();
    test_type_b_voltage();
    test_type_a_voltage();
    test_mode_enable_and_shadow();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
