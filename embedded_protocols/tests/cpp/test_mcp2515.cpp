// MCP2515 driver against a register-level fake of the chip, reached through
// the real C SPI core (ep_spi_bus_t) and the C++ SpiDevice adapter.
#include "epp/bus.hpp"
#include "epp/mcp2515.hpp"
#include "ep_test.h"

#include <cstring>
#include <vector>

struct FakeMcp2515 {
    std::uint8_t r[128]{};
    std::vector<std::vector<std::uint8_t>> wire; // frames sent in normal mode
    int transactions = 0;

    void reset()
    {
        std::memset(r, 0, sizeof(r));
        r[0x0E] = 0x80; // CANSTAT: config mode
        r[0x0F] = 0x87;
    }
    std::uint8_t opmode() const { return r[0x0E] >> 5; }

    void regWrite(std::uint8_t a, std::uint8_t v)
    {
        a &= 0x7F;
        r[a] = v;
        if (a == 0x0F) // CANCTRL.REQOP -> CANSTAT.OPMOD
            r[0x0E] = static_cast<std::uint8_t>((r[0x0E] & 0x1F) | (v & 0xE0));
        if ((a == 0x30 || a == 0x40 || a == 0x50) && (v & 0x08))
            transmit((a - 0x30) / 0x10);
    }

    void transmit(int buf)
    {
        const std::uint8_t base = static_cast<std::uint8_t>(0x31 + 0x10 * buf);
        if (opmode() == 2) { // loopback: TXBn -> RXB0
            std::memcpy(&r[0x61], &r[base], 13);
            r[0x2C] |= 0x01;
        } else if (opmode() == 0) {
            wire.emplace_back(&r[base], &r[base] + 5 + (r[base + 4] & 0x0F));
        }
        r[0x30 + 0x10 * buf] &= static_cast<std::uint8_t>(~0x08);
        r[0x2C] |= static_cast<std::uint8_t>(0x04 << buf); // TXnIF
    }

    std::uint8_t readStatus() const
    {
        std::uint8_t s = 0;
        s |= r[0x2C] & 0x01;               // RX0IF
        s |= (r[0x2C] & 0x02);              // RX1IF
        if (r[0x30] & 0x08) s |= 0x04;      // TXB0 TXREQ
        if (r[0x2C] & 0x04) s |= 0x08;      // TX0IF
        if (r[0x40] & 0x08) s |= 0x10;
        if (r[0x50] & 0x08) s |= 0x40;
        return s;
    }

    // One chip-select window.
    void transaction(const std::uint8_t *tx, std::uint8_t *rx, std::size_t n)
    {
        transactions++;
        std::memset(rx, 0xFF, n);
        if (n == 0)
            return;
        std::uint8_t op = tx[0];
        if (op == 0xC0) {
            reset();
        } else if (op == 0x03 && n >= 2) {
            for (std::size_t i = 2; i < n; i++)
                rx[i] = r[(tx[1] + i - 2) & 0x7F];
        } else if (op == 0x02 && n >= 2) {
            for (std::size_t i = 2; i < n; i++)
                regWrite(static_cast<std::uint8_t>(tx[1] + i - 2), tx[i]);
        } else if (op == 0x05 && n >= 4) {
            std::uint8_t a = tx[1];
            regWrite(a, static_cast<std::uint8_t>((r[a] & ~tx[2]) | (tx[3] & tx[2])));
        } else if (op == 0xA0) {
            for (std::size_t i = 1; i < n; i++)
                rx[i] = readStatus();
        } else if ((op & 0xF8) == 0x40) { // LOAD TX BUFFER
            int buf = (op >> 1) & 3;
            std::uint8_t base = static_cast<std::uint8_t>(0x31 + 0x10 * buf + ((op & 1) ? 5 : 0));
            for (std::size_t i = 1; i < n; i++)
                r[base + i - 1] = tx[i];
        } else if ((op & 0xF8) == 0x80) { // RTS
            for (int b = 0; b < 3; b++)
                if (op & (1 << b))
                    transmit(b);
        } else if ((op & 0xF9) == 0x90) { // READ RX BUFFER
            int buf = (op >> 2) & 1;
            std::uint8_t base = static_cast<std::uint8_t>(0x61 + 0x10 * buf + ((op & 2) ? 5 : 0));
            for (std::size_t i = 1; i < n; i++)
                rx[i] = r[base + i - 1];
            r[0x2C] &= static_cast<std::uint8_t>(~(1 << buf)); // auto-clear RXnIF
        }
    }
};

// C bus backend: controller-managed CS, whole transaction at once.
static ep_status_t fake_segments(void *ctx, std::uint8_t, const ep_spi_segment_t *segs, std::size_t n)
{
    auto *chip = static_cast<FakeMcp2515 *>(ctx);
    std::uint8_t tx[64] = {}, rx[64];
    std::size_t len = 0;
    for (std::size_t i = 0; i < n; i++) {
        if (segs[i].tx)
            std::memcpy(tx + len, segs[i].tx, segs[i].len);
        len += segs[i].len;
    }
    chip->transaction(tx, rx, len);
    std::size_t off = 0;
    for (std::size_t i = 0; i < n; i++) {
        if (segs[i].rx)
            std::memcpy(segs[i].rx, rx + off, segs[i].len);
        off += segs[i].len;
    }
    return EP_OK;
}
static const ep_spi_bus_ops_t fake_ops = {nullptr, nullptr, nullptr, fake_segments, nullptr, nullptr};

struct Rig {
    FakeMcp2515 chip;
    ep_spi_bus_t bus{};
    ep_spi_config_t cfg{10000000, EP_SPI_MODE0, 8, false, false};
    Rig() { ep_spi_bus_init(&bus, &fake_ops, &chip, nullptr); }
};

static void test_init_and_bitrate()
{
    Rig rig;
    epp::SpiDevice spi(rig.bus, 0, rig.cfg);
    CHECK(spi.initStatus() == epp::Status::Ok);
    epp::Mcp2515<epp::SpiDevice> can(spi);
    CHECK(can.reset() == epp::Status::Ok);
    CHECK(can.setBitrate(8000000, 500000) == epp::Status::Ok);
    // 8 MHz crystal, 500 kbit/s: Tq = 250 ns, 8 Tq per bit
    std::uint8_t cnf1 = rig.chip.r[0x2A], cnf2 = rig.chip.r[0x29], cnf3 = rig.chip.r[0x28];
    unsigned brp = (cnf1 & 0x3F) + 1u;
    unsigned prseg = (cnf2 & 7) + 1u, ps1 = ((cnf2 >> 3) & 7) + 1u, ps2 = (cnf3 & 7) + 1u;
    CHECK_EQ(brp, 1);
    CHECK_EQ(1 + prseg + ps1 + ps2, 8);
    CHECK(cnf2 & 0x80); // BTLMODE: PS2 from CNF3
    CHECK_EQ(can.bittiming().sample_point_permille, 750); /* PS2 >= 2 Tq: 6/8 = 75 % */
    CHECK(can.setMode(epp::Mcp2515<epp::SpiDevice>::Mode::Normal) == epp::Status::Ok);
    CHECK_EQ(rig.chip.opmode(), 0);
}

static void test_loopback_std_and_ext()
{
    Rig rig;
    epp::SpiDevice spi(rig.bus, 0, rig.cfg);
    using Chip = epp::Mcp2515<epp::SpiDevice>;
    Chip can(spi);
    can.reset();
    can.setBitrate(16000000, 250000);
    CHECK(can.setMode(Chip::Mode::Loopback) == epp::Status::Ok);

    ep_can_frame_t tx{};
    tx.id = 0x18DAF110;
    tx.flags = EP_CAN_FLAG_EXT;
    tx.len = 8;
    for (int i = 0; i < 8; i++)
        tx.data[i] = static_cast<std::uint8_t>(0xA0 + i);
    CHECK(can.send(tx) == epp::Status::Ok);
    ep_can_frame_t rx{};
    CHECK(can.receive(rx) == epp::Status::Ok);
    CHECK_EQ(rx.id, 0x18DAF110);
    CHECK(rx.flags & EP_CAN_FLAG_EXT);
    CHECK_EQ(rx.len, 8);
    CHECK_MEM(rx.data, tx.data, 8);
    CHECK(can.receive(rx) == epp::Status::Again); // RX0IF auto-cleared

    ep_can_frame_t s{};
    s.id = 0x7DF;
    s.len = 3;
    s.data[0] = 0x02;
    s.data[1] = 0x01;
    s.data[2] = 0x0C; // OBD-II engine RPM request
    CHECK(can.send(s) == epp::Status::Ok);
    CHECK(can.receive(rx) == epp::Status::Ok);
    CHECK_EQ(rx.id, 0x7DF);
    CHECK(!(rx.flags & EP_CAN_FLAG_EXT));
    CHECK_EQ(rx.data[2], 0x0C);

    ep_can_frame_t fd{};
    fd.flags = EP_CAN_FLAG_FD;
    CHECK(can.send(fd) == epp::Status::Param); // MCP2515 is classic CAN only
}

static void test_id_encoding_matches_datasheet()
{
    ep_can_frame_t f{};
    f.id = 0x123;
    std::uint8_t o[4];
    epp::Mcp2515<epp::SpiDevice>::encodeId(f, o);
    CHECK_EQ(o[0], 0x24); // SID10..3
    CHECK_EQ(o[1], 0x60); // SID2..0 << 5
    f.id = 0x1FFFFFFF;
    f.flags = EP_CAN_FLAG_EXT;
    epp::Mcp2515<epp::SpiDevice>::encodeId(f, o);
    CHECK_EQ(o[0], 0xFF);
    CHECK_EQ(o[1], 0xEB); // SID2..0=111, EXIDE, EID17..16=11
    CHECK_EQ(o[2], 0xFF);
    CHECK_EQ(o[3], 0xFF);
}

static void test_plugs_into_c_core()
{
    Rig rig;
    epp::SpiDevice spi(rig.bus, 0, rig.cfg);
    using Chip = epp::Mcp2515<epp::SpiDevice>;
    Chip chip(spi);
    chip.reset();
    chip.setBitrate(8000000, 125000);

    ep_can_t core;
    CHECK_EQ(ep_can_init(&core, &Chip::canOps(), &chip), EP_OK);
    CHECK_EQ(core.ops->set_mode(core.ctx, EP_CAN_MODE_NORMAL), EP_OK);
    ep_can_frame_t f{};
    f.id = 0x321;
    f.len = 2;
    f.data[0] = 0xCA;
    f.data[1] = 0xFE;
    CHECK_EQ(ep_can_send(&core, &f), EP_OK);
    CHECK_EQ(rig.chip.wire.size(), 1);
    CHECK_EQ(rig.chip.wire[0].size(), 7);
    CHECK_EQ(rig.chip.wire[0][5], 0xCA);

    rig.chip.r[0x1C] = 130; // TEC
    rig.chip.r[0x2D] = 0x00;
    CHECK_EQ(ep_can_update_state(&core), EP_CAN_ERROR_PASSIVE);
    rig.chip.r[0x2D] = 0x20; // TXBO
    CHECK_EQ(ep_can_update_state(&core), EP_CAN_BUS_OFF);

    // loopback frames drained by the INT handler land in the C RX queue
    CHECK(chip.setMode(Chip::Mode::Loopback) == epp::Status::Ok);
    core.state = EP_CAN_ERROR_ACTIVE;
    CHECK_EQ(ep_can_send(&core, &f), EP_OK);
    chip.pollInto(core);
    ep_can_frame_t rx;
    CHECK_EQ(ep_can_receive(&core, &rx), EP_OK);
    CHECK_EQ(rx.id, 0x321);
}

int main()
{
    RUN(test_init_and_bitrate);
    RUN(test_loopback_std_and_ext);
    RUN(test_id_encoding_matches_datasheet);
    RUN(test_plugs_into_c_core);
    TEST_MAIN_END();
}
