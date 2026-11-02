// 24xx EEPROM driver: page splitting and ACK polling against a fake chip
// that behaves like the real silicon (page wrap, busy NACK during tWR).
#include "epp/bus.hpp"
#include "epp/eeprom24xx.hpp"
#include "ep_test.h"

#include <cstring>

struct Fake24c32 {
    static constexpr std::size_t kSize = 4096, kPage = 32;
    std::uint8_t mem[kSize];
    std::uint16_t ptr = 0;
    int busy_polls = 0;   // NACKs left before the write cycle completes
    int writes = 0, max_write_payload = 0, polls = 0;
    Fake24c32() { std::memset(mem, 0xFF, sizeof(mem)); }
};

static ep_status_t fake_xfer(void *ctx, ep_i2c_msg_t *m, std::size_t n)
{
    auto *e = static_cast<Fake24c32 *>(ctx);
    if (m[0].addr != 0x50)
        return EP_ERR_NACK;
    if (e->busy_polls > 0) {
        e->busy_polls--;
        e->polls++;
        return EP_ERR_NACK; // internal write cycle in progress
    }
    for (std::size_t i = 0; i < n; i++) {
        if (m[i].flags & EP_I2C_M_RD) {
            for (std::uint16_t k = 0; k < m[i].len; k++)
                m[i].buf[k] = e->mem[e->ptr++ % Fake24c32::kSize];
            continue;
        }
        if (m[i].len < 2)
            continue; // address-only probe (ACK poll)
        e->ptr = static_cast<std::uint16_t>(((m[i].buf[0] << 8) | m[i].buf[1]) % Fake24c32::kSize);
        int payload = m[i].len - 2;
        if (payload > 0 && n == 1) {
            // real chips wrap inside the page: model it so a bad driver shows up
            std::uint16_t page = static_cast<std::uint16_t>(e->ptr & ~(Fake24c32::kPage - 1));
            for (int k = 0; k < payload; k++) {
                std::uint16_t a = static_cast<std::uint16_t>(page + ((e->ptr - page + k) % Fake24c32::kPage));
                e->mem[a] = m[i].buf[2 + k];
            }
            e->writes++;
            if (payload > e->max_write_payload)
                e->max_write_payload = payload;
            e->busy_polls = 3;
        }
    }
    return EP_OK;
}
static const ep_i2c_bus_ops_t fake_ops = {fake_xfer, nullptr, nullptr, nullptr};

static int delays;
static void poll_delay() { delays++; }

static void test_page_split_and_ack_poll()
{
    Fake24c32 chip;
    ep_i2c_bus_t bus;
    ep_i2c_bus_init(&bus, &fake_ops, &chip);
    epp::I2cDevice dev(bus, 0x50, 2);
    epp::Eeprom24xx<epp::I2cDevice> ee(dev, epp::Eeprom24xx<epp::I2cDevice>::k24c32, poll_delay);

    std::uint8_t data[100];
    for (int i = 0; i < 100; i++)
        data[i] = static_cast<std::uint8_t>(i + 1);
    // start 10 bytes before a page boundary: 10 + 32 + 32 + 26
    CHECK(ee.write(54, epp::Bytes(data, 100)) == epp::Status::Ok);
    CHECK_EQ(chip.writes, 4);
    CHECK_EQ(ee.pagesWritten(), 4);
    CHECK(chip.max_write_payload <= 32);
    CHECK_EQ(chip.polls, 12); /* 3 busy NACKs after each page */
    CHECK_EQ(delays, 12);

    std::uint8_t back[100] = {};
    CHECK(ee.read(54, epp::MutBytes(back, 100)) == epp::Status::Ok);
    CHECK_MEM(back, data, 100);
    CHECK_EQ(chip.mem[53], 0xFF); // neighbours untouched
    CHECK_EQ(chip.mem[154], 0xFF);

    CHECK(ee.read(4090, epp::MutBytes(back, 10)) == epp::Status::Param); // out of range
}

static void test_typed_registers()
{
    Fake24c32 chip;
    ep_i2c_bus_t bus;
    ep_i2c_bus_init(&bus, &fake_ops, &chip);
    epp::I2cDevice dev(bus, 0x50, 2);
    chip.mem[0x0102] = 0x12;
    chip.mem[0x0103] = 0x34;
    chip.ptr = 0x0102;
    std::uint8_t two[2] = {};
    CHECK(dev.read(epp::MutBytes(two, 2)) == epp::Status::Ok); // current-address read
    CHECK_EQ(two[0], 0x12);
    epp::I2cDevice ghost(bus, 0x51);
    auto r = ghost.readReg<std::uint16_t>(0);
    CHECK(!r.ok());
    CHECK(r.status() == epp::Status::Nack);
}

int main()
{
    RUN(test_page_split_and_ack_poll);
    RUN(test_typed_registers);
    TEST_MAIN_END();
}
