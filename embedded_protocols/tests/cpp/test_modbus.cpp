// Modbus RTU master <-> slave over a simulated RS-485 link.
#include "epp/modbus_rtu.hpp"
#include "ep_test.h"

#include <deque>

using namespace epp;
using namespace epp::modbus;

// Register map of a simulated VFD / sensor node.
struct DeviceMap {
    std::uint16_t holding[32]{};
    std::uint16_t input[8]{1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
    std::uint8_t coils[4]{};

    Exception readHolding(std::uint16_t a, Span<std::uint16_t> out)
    {
        if (a + out.size() > 32) return Exception::IllegalDataAddress;
        for (std::size_t i = 0; i < out.size(); i++) out[i] = holding[a + i];
        return Exception::None;
    }
    Exception writeHolding(std::uint16_t a, Span<const std::uint16_t> in)
    {
        if (a + in.size() > 32) return Exception::IllegalDataAddress;
        for (std::size_t i = 0; i < in.size(); i++) holding[a + i] = in[i];
        return Exception::None;
    }
    Exception readInput(std::uint16_t a, Span<std::uint16_t> out)
    {
        if (a + out.size() > 8) return Exception::IllegalDataAddress;
        for (std::size_t i = 0; i < out.size(); i++) out[i] = input[a + i];
        return Exception::None;
    }
    Exception readCoils(std::uint16_t a, std::uint16_t n, std::uint8_t *bits)
    {
        if (a + n > 32) return Exception::IllegalDataAddress;
        for (std::uint16_t i = 0; i < n; i++)
            if (coils[(a + i) / 8] & (1 << ((a + i) % 8)))
                bits[i / 8] = static_cast<std::uint8_t>(bits[i / 8] | (1 << (i % 8)));
        return Exception::None;
    }
    Exception writeCoil(std::uint16_t a, bool on)
    {
        if (a >= 32) return Exception::IllegalDataAddress;
        if (on) coils[a / 8] = static_cast<std::uint8_t>(coils[a / 8] | (1 << (a % 8)));
        else coils[a / 8] = static_cast<std::uint8_t>(coils[a / 8] & ~(1 << (a % 8)));
        return Exception::None;
    }
};

// Master-side port: the "bus" delivers each request to the slave, and the
// slave's reply comes back into the master's receive queue.
struct LinkPort {
    RtuSlave<DeviceMap> *slave;
    std::deque<int> rx;
    bool corrupt_reply = false;
    int requests = 0;

    void flushInput() { rx.clear(); }
    std::size_t write(const std::uint8_t *d, std::size_t n)
    {
        requests++;
        std::uint8_t resp[kMaxAdu];
        std::size_t r = slave->handle(d, n, resp);
        if (r && corrupt_reply)
            resp[2] ^= 0x01;
        for (std::size_t i = 0; i < r; i++)
            rx.push_back(resp[i]);
        return n;
    }
    void drain() {}
    int readByte(std::uint32_t)
    {
        if (rx.empty())
            return -1; // silence: t3.5 gap or response timeout
        int b = rx.front();
        rx.pop_front();
        return b;
    }
};

static void test_known_frame()
{
    // Canonical example: read 10 holding regs from slave 1 at 0
    std::uint8_t req[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    CHECK_EQ(appendCrc(req, 6), 8);
    CHECK_EQ(req[6], 0xC5);
    CHECK_EQ(req[7], 0xCD);
    CHECK(crcOk(req, 8));
}

static void test_read_write_registers()
{
    DeviceMap map;
    RtuSlave<DeviceMap> slave(map, 17);
    LinkPort port{&slave, {}, false, 0};
    RtuMaster<LinkPort> master(port, 1750);

    CHECK(master.writeSingleRegister(17, 3, 0xBEEF) == Status::Ok);
    CHECK_EQ(map.holding[3], 0xBEEF);

    const std::uint16_t setpoints[4] = {1500, 50, 0x8000, 7};
    CHECK(master.writeMultipleRegisters(17, 10, Span<const std::uint16_t>(setpoints, 4)) == Status::Ok);
    CHECK_EQ(map.holding[12], 0x8000);

    std::uint16_t got[6];
    CHECK(master.readHoldingRegisters(17, 9, Span<std::uint16_t>(got, 6)) == Status::Ok);
    CHECK_EQ(got[0], 0);
    CHECK_EQ(got[1], 1500);
    CHECK_EQ(got[4], 7);

    std::uint16_t in[3];
    CHECK(master.readInputRegisters(17, 5, Span<std::uint16_t>(in, 3)) == Status::Ok);
    CHECK_EQ(in[0], 6000);
    CHECK_EQ(in[2], 8000);
}

static void test_coils()
{
    DeviceMap map;
    RtuSlave<DeviceMap> slave(map, 2);
    LinkPort port{&slave, {}, false, 0};
    RtuMaster<LinkPort> master(port, 1750);
    CHECK(master.writeSingleCoil(2, 0, true) == Status::Ok);
    CHECK(master.writeSingleCoil(2, 9, true) == Status::Ok);
    std::uint8_t bits[2] = {};
    CHECK(master.readCoils(2, 0, 10, MutBytes(bits, 2)) == Status::Ok);
    CHECK_EQ(bits[0], 0x01);
    CHECK_EQ(bits[1], 0x02);
}

static void test_exceptions_and_errors()
{
    DeviceMap map;
    RtuSlave<DeviceMap> slave(map, 5);
    LinkPort port{&slave, {}, false, 0};
    RtuMaster<LinkPort> master(port, 1750);

    std::uint16_t r[4];
    CHECK(master.readHoldingRegisters(5, 30, Span<std::uint16_t>(r, 4)) == Status::Nack);
    CHECK(master.lastException() == Exception::IllegalDataAddress);

    CHECK(master.readHoldingRegisters(6, 0, Span<std::uint16_t>(r, 1)) == Status::Timeout); // nobody home

    port.corrupt_reply = true;
    CHECK(master.readHoldingRegisters(5, 0, Span<std::uint16_t>(r, 1)) == Status::Crc);
    port.corrupt_reply = false;

    // slave ignores a frame with a bad CRC entirely
    std::uint8_t bad[8] = {5, 3, 0, 0, 0, 1, 0x00, 0x00};
    std::uint8_t resp[kMaxAdu];
    CHECK_EQ(slave.handle(bad, 8, resp), 0);
    CHECK_EQ(slave.crcErrors(), 1);

    // unsupported function code -> exception 01
    std::uint8_t req[8] = {5, 0x2B, 0x0E, 0x01};
    std::size_t n = appendCrc(req, 4);
    std::size_t rn = slave.handle(req, n, resp);
    CHECK_EQ(rn, 5);
    CHECK_EQ(resp[1], 0xAB);
    CHECK_EQ(resp[2], 1);
}

static void test_broadcast()
{
    DeviceMap map;
    RtuSlave<DeviceMap> slave(map, 9);
    LinkPort port{&slave, {}, false, 0};
    RtuMaster<LinkPort> master(port, 1750);
    CHECK(master.writeSingleRegister(0, 1, 42) == Status::Ok); // no reply expected
    CHECK_EQ(map.holding[1], 42);
    CHECK(port.rx.empty());
}

int main()
{
    RUN(test_known_frame);
    RUN(test_read_write_registers);
    RUN(test_coils);
    RUN(test_exceptions_and_errors);
    RUN(test_broadcast);
    TEST_MAIN_END();
}
