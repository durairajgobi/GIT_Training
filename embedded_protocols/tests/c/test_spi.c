#include "ep/ep_spi.h"
#include "ep_test.h"
#include "sim_spi_slave.h"

static void bitbang_roundtrip(ep_spi_mode_t mode)
{
    const uint8_t slave_tx[] = {0xA5, 0x3C, 0x0F, 0xF0};
    sim_spi_slave_t slave;
    sim_spi_slave_init(&slave, slave_tx, sizeof(slave_tx));
    ep_gpio_t gpio = sim_spi_slave_gpio(&slave);

    ep_spi_bitbang_t bb;
    uint32_t cs = SIM_PIN_CS0;
    ep_spi_bitbang_init(&bb, &gpio, NULL, SIM_PIN_SCK, SIM_PIN_MOSI, SIM_PIN_MISO, &cs, 1);

    ep_spi_bus_t bus;
    CHECK_EQ(ep_spi_bus_init(&bus, &ep_spi_bitbang_ops, &bb, NULL), EP_OK);
    ep_spi_device_t dev;
    ep_spi_config_t cfg = {.max_hz = 1000000, .mode = mode, .bits_per_word = 8};
    CHECK_EQ(ep_spi_device_init(&dev, &bus, 0, &cfg), EP_OK);

    const uint8_t tx[] = {0x9F, 0x12, 0x34, 0xEE};
    uint8_t rx[4] = {0};
    CHECK_EQ(ep_spi_transfer(&dev, tx, rx, 4), EP_OK);
    CHECK_MEM(rx, slave_tx, 4);
    CHECK_EQ(slave.rx_len, 4);
    CHECK_MEM(slave.rx, tx, 4);
    CHECK(slave.cs_n); /* deselected afterwards */
    CHECK_EQ(slave.cs_assertions, 1);
    CHECK_EQ(slave.sck, (mode & 2) != 0); /* clock back at idle polarity */
}

static void test_bitbang_mode0(void) { bitbang_roundtrip(EP_SPI_MODE0); }
static void test_bitbang_mode3(void) { bitbang_roundtrip(EP_SPI_MODE3); }

static void test_reg_read_single_cs(void)
{
    const uint8_t slave_tx[] = {0x00, 0xD0, 0x55}; /* dummy during cmd, then data */
    sim_spi_slave_t slave;
    sim_spi_slave_init(&slave, slave_tx, sizeof(slave_tx));
    ep_gpio_t gpio = sim_spi_slave_gpio(&slave);
    ep_spi_bitbang_t bb;
    uint32_t cs = SIM_PIN_CS0;
    ep_spi_bitbang_init(&bb, &gpio, NULL, SIM_PIN_SCK, SIM_PIN_MOSI, SIM_PIN_MISO, &cs, 1);
    ep_spi_bus_t bus;
    ep_spi_bus_init(&bus, &ep_spi_bitbang_ops, &bb, NULL);
    ep_spi_device_t dev;
    ep_spi_config_t cfg = {.max_hz = 8000000, .mode = EP_SPI_MODE0, .bits_per_word = 8};
    ep_spi_device_init(&dev, &bus, 0, &cfg);

    uint8_t id[2];
    CHECK_EQ(ep_spi_reg_read(&dev, 0x50, 0x80, id, 2), EP_OK);
    CHECK_EQ(slave.rx[0], 0xD0); /* register | read flag */
    CHECK_EQ(id[0], 0xD0);
    CHECK_EQ(id[1], 0x55);
    CHECK_EQ(slave.cs_assertions, 1); /* command + data in one CS window */
}

/* ---- mock controller: counts reconfigurations and CS edges ---- */
typedef struct {
    int configures, cs_assert, cs_release;
    ep_spi_config_t last;
} mock_bus_t;

static ep_status_t m_cfg(void *ctx, const ep_spi_config_t *c)
{
    mock_bus_t *m = ctx;
    m->configures++;
    m->last = *c;
    return EP_OK;
}
static ep_status_t m_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    for (size_t i = 0; i < len; i++)
        if (rx)
            rx[i] = tx ? (uint8_t)~tx[i] : 0;
    return EP_OK;
}
static void m_cs(void *ctx, uint8_t cs, bool a, bool hi)
{
    (void)cs;
    (void)hi;
    mock_bus_t *m = ctx;
    if (a)
        m->cs_assert++;
    else
        m->cs_release++;
}
static const ep_spi_bus_ops_t mock_ops = {.configure = m_cfg, .transfer = m_xfer, .chip_select = m_cs};

static void test_config_caching_and_cs_change(void)
{
    mock_bus_t m = {0};
    ep_spi_bus_t bus;
    ep_spi_bus_init(&bus, &mock_ops, &m, NULL);
    ep_spi_device_t flash, adc;
    ep_spi_config_t c1 = {.max_hz = 20000000, .mode = EP_SPI_MODE0, .bits_per_word = 8};
    ep_spi_config_t c2 = {.max_hz = 1000000, .mode = EP_SPI_MODE1, .bits_per_word = 8};
    ep_spi_device_init(&flash, &bus, 0, &c1);
    ep_spi_device_init(&adc, &bus, 1, &c2);

    uint8_t b = 0x5A, r;
    ep_spi_transfer(&flash, &b, &r, 1);
    ep_spi_transfer(&flash, &b, &r, 1);
    CHECK_EQ(m.configures, 1); /* same device twice: no reprogramming */
    CHECK_EQ(r, 0xA5);
    ep_spi_transfer(&adc, &b, &r, 1);
    CHECK_EQ(m.configures, 2);
    CHECK_EQ(m.last.mode, EP_SPI_MODE1);

    m.cs_assert = m.cs_release = 0;
    ep_spi_segment_t segs[3] = {
        {.tx = &b, .len = 1, .cs_change = true},
        {.tx = &b, .len = 1},
        {.tx = &b, .len = 1},
    };
    CHECK_EQ(ep_spi_transfer_segments(&adc, segs, 3), EP_OK);
    CHECK_EQ(m.cs_assert, 2);  /* toggled once between seg 0 and 1 */
    CHECK_EQ(m.cs_release, 2);
}

static void test_invalid_config(void)
{
    mock_bus_t m = {0};
    ep_spi_bus_t bus;
    ep_spi_bus_init(&bus, &mock_ops, &m, NULL);
    ep_spi_device_t d;
    ep_spi_config_t bad = {.max_hz = 0, .mode = EP_SPI_MODE0, .bits_per_word = 8};
    CHECK_EQ(ep_spi_device_init(&d, &bus, 0, &bad), EP_ERR_PARAM);
}

int main(void)
{
    RUN(test_bitbang_mode0);
    RUN(test_bitbang_mode3);
    RUN(test_reg_read_single_cs);
    RUN(test_config_caching_and_cs_change);
    RUN(test_invalid_config);
    TEST_MAIN_END();
}
