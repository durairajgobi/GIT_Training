#include "ep/ep_crc.h"
#include "ep/ep_i2c.h"
#include "ep_test.h"
#include "sim_i2c_eeprom.h"

static void setup_bitbang(sim_i2c_eeprom_t *e, ep_gpio_t *gpio, ep_i2c_bitbang_t *bb, ep_i2c_bus_t *bus)
{
    sim_i2c_eeprom_init(e, 0x50);
    *gpio = sim_i2c_eeprom_gpio(e);
    ep_i2c_bitbang_init(bb, gpio, NULL, SIM_PIN_SCL, SIM_PIN_SDA, 0);
    ep_i2c_bus_init(bus, &ep_i2c_bitbang_ops, bb);
}

static void test_bitbang_write_then_read(void)
{
    sim_i2c_eeprom_t e;
    ep_gpio_t g;
    ep_i2c_bitbang_t bb;
    ep_i2c_bus_t bus;
    setup_bitbang(&e, &g, &bb, &bus);
    ep_i2c_device_t dev = {.bus = &bus, .addr = 0x50, .reg_addr_len = 1};

    const uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK_EQ(ep_i2c_reg_write(&dev, 0x10, data, 4), EP_OK);
    CHECK_MEM(&e.mem[0x10], data, 4);
    CHECK_EQ(e.stops, 1);

    uint8_t rd[4] = {0};
    CHECK_EQ(ep_i2c_reg_read(&dev, 0x10, rd, 4), EP_OK); /* repeated START */
    CHECK_MEM(rd, data, 4);
    CHECK_EQ(e.starts, 3); /* 1 for write, START + Sr for read */
    CHECK_EQ(e.stops, 2);
    CHECK(e.m_scl && e.m_sda && e.s_sda); /* bus idle: both lines released */
}

static void test_bitbang_nack(void)
{
    sim_i2c_eeprom_t e;
    ep_gpio_t g;
    ep_i2c_bitbang_t bb;
    ep_i2c_bus_t bus;
    setup_bitbang(&e, &g, &bb, &bus);
    ep_i2c_device_t ghost = {.bus = &bus, .addr = 0x51, .reg_addr_len = 1};
    uint8_t b;
    CHECK_EQ(ep_i2c_reg_read(&ghost, 0, &b, 1), EP_ERR_NACK);
    CHECK_EQ(e.stops, 1); /* bus released with STOP after NACK */
}

static void test_bitbang_scan(void)
{
    sim_i2c_eeprom_t e;
    ep_gpio_t g;
    ep_i2c_bitbang_t bb;
    ep_i2c_bus_t bus;
    setup_bitbang(&e, &g, &bb, &bus);
    uint8_t found[16];
    CHECK_EQ(ep_i2c_scan(&bus, found), 1);
    CHECK(found[0x50 >> 3] & (1u << (0x50 & 7)));
}

static void test_bitbang_recovery(void)
{
    sim_i2c_eeprom_t e;
    ep_gpio_t g;
    ep_i2c_bitbang_t bb;
    ep_i2c_bus_t bus;
    setup_bitbang(&e, &g, &bb, &bus);
    CHECK_EQ(ep_i2c_recover(&bus), EP_OK); /* healthy bus */
    e.stuck_low = true;
    CHECK_EQ(ep_i2c_recover(&bus), EP_ERR_IO); /* permanently shorted SDA */
    ep_i2c_device_t dev = {.bus = &bus, .addr = 0x50, .reg_addr_len = 1};
    uint8_t b;
    CHECK_EQ(ep_i2c_reg_read(&dev, 0, &b, 1), EP_ERR_ARBITRATION);
}

/* ---------------- SMBus with PEC over a message-level mock --------- */
typedef struct {
    uint8_t last_write[8];
    size_t last_write_len;
    bool corrupt;
} smbus_dev_t;

static ep_status_t smbus_xfer(void *ctx, ep_i2c_msg_t *m, size_t n)
{
    smbus_dev_t *d = ctx;
    if (n == 1 && !(m[0].flags & EP_I2C_M_RD)) {
        memcpy(d->last_write, m[0].buf, m[0].len);
        d->last_write_len = m[0].len;
        return EP_OK;
    }
    if (n == 2 && (m[1].flags & EP_I2C_M_RD)) {
        uint8_t cmd = m[0].buf[0];
        uint8_t hdr[3] = {(uint8_t)(m[0].addr << 1), cmd, (uint8_t)((m[0].addr << 1) | 1)};
        if (cmd == 0x8B) { /* READ_VOUT style word: 0x1234 little-endian */
            m[1].buf[0] = 0x34;
            m[1].buf[1] = 0x12;
            uint8_t crc = ep_crc8_smbus(0, hdr, 3);
            m[1].buf[2] = (uint8_t)(ep_crc8_smbus(crc, m[1].buf, 2) ^ (d->corrupt ? 1 : 0));
            return EP_OK;
        }
        if (cmd == 0x99) { /* block read "ACME" */
            const uint8_t blk[5] = {4, 'A', 'C', 'M', 'E'};
            memset(m[1].buf, 0xFF, m[1].len);
            memcpy(m[1].buf, blk, 5);
            uint8_t crc = ep_crc8_smbus(0, hdr, 3);
            m[1].buf[5] = ep_crc8_smbus(crc, blk, 5);
            return EP_OK;
        }
    }
    return EP_ERR_NACK;
}
static const ep_i2c_bus_ops_t smbus_ops = {.xfer = smbus_xfer};

static void test_smbus_pec(void)
{
    smbus_dev_t d = {0};
    ep_i2c_bus_t bus;
    ep_i2c_bus_init(&bus, &smbus_ops, &d);
    ep_i2c_device_t dev = {.bus = &bus, .addr = 0x40, .pec = true};

    uint16_t w = 0;
    CHECK_EQ(ep_smbus_read_word_data(&dev, 0x8B, &w), EP_OK);
    CHECK_EQ(w, 0x1234);
    d.corrupt = true;
    CHECK_EQ(ep_smbus_read_word_data(&dev, 0x8B, &w), EP_ERR_CRC);
    d.corrupt = false;

    CHECK_EQ(ep_smbus_write_byte_data(&dev, 0x01, 0x80), EP_OK);
    CHECK_EQ(d.last_write_len, 3);
    uint8_t frame[3] = {0x80, 0x01, 0x80}; /* addr+W, cmd, data */
    CHECK_EQ(d.last_write[2], ep_crc8_smbus(0, frame, 3));

    uint8_t blk[32];
    size_t n = 0;
    CHECK_EQ(ep_smbus_block_read(&dev, 0x99, blk, sizeof(blk), &n), EP_OK);
    CHECK_EQ(n, 4);
    CHECK_MEM(blk, "ACME", 4);
}

static void test_msg_model_two_byte_address(void)
{
    sim_i2c_eeprom_t e;
    sim_i2c_eeprom_init(&e, 0x50);
    ep_i2c_bus_t bus;
    ep_i2c_bus_init(&bus, &sim_i2c_msg_ops, &e);
    ep_i2c_device_t dev = {.bus = &bus, .addr = 0x50, .reg_addr_len = 1};
    uint8_t v = 0x42;
    CHECK_EQ(ep_i2c_reg_write(&dev, 0xFE, &v, 1), EP_OK);
    CHECK_EQ(e.mem[0xFE], 0x42);
    uint8_t big[EP_I2C_REG_WRITE_MAX + 1] = {0};
    CHECK_EQ(ep_i2c_reg_write(&dev, 0, big, sizeof(big)), EP_ERR_PARAM);
}

int main(void)
{
    RUN(test_bitbang_write_then_read);
    RUN(test_bitbang_nack);
    RUN(test_bitbang_scan);
    RUN(test_bitbang_recovery);
    RUN(test_smbus_pec);
    RUN(test_msg_model_two_byte_address);
    TEST_MAIN_END();
}
