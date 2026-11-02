#include "epp/core.hpp"
#include "epp/eth.hpp"
#include "ep_test.h"

#include <thread>

using namespace epp;

static void test_result_and_span()
{
    Result<int> a = 5;
    Result<int> b = Status::Timeout;
    CHECK(a.ok() && a.value() == 5);
    CHECK(!b && b.status() == Status::Timeout);
    CHECK_EQ(b.valueOr(-1), -1);

    std::uint8_t raw[6] = {1, 2, 3, 4, 5, 6};
    MutBytes s(raw);
    CHECK_EQ(s.size(), 6);
    Bytes c = s; // mutable -> const conversion
    CHECK_EQ(c.subspan(4).size(), 2);
    CHECK_EQ(c.subspan(4)[1], 6);
    CHECK_EQ(c.first(2).size(), 2);
    CHECK(c.subspan(10).empty());
    std::array<std::uint8_t, 3> arr{7, 8, 9};
    Bytes ca(arr);
    CHECK_EQ(ca[2], 9);
}

static void test_ring_buffer_basic()
{
    RingBuffer<std::uint32_t, 4> rb;
    for (std::uint32_t i = 0; i < 4; i++)
        CHECK(rb.push(i));
    CHECK(rb.full());
    CHECK(!rb.push(99));
    std::uint32_t v;
    CHECK(rb.pop(v) && v == 0);
    CHECK(rb.push(4));
    for (std::uint32_t i = 1; i <= 4; i++)
        CHECK(rb.pop(v) && v == i);
    CHECK(rb.empty());
}

static void test_ring_buffer_threads()
{
    // ISR/task hand-off modelled with two threads: no loss, order kept.
    static RingBuffer<std::uint32_t, 64> rb;
    constexpr std::uint32_t kN = 200000;
    std::thread producer([] {
        for (std::uint32_t i = 0; i < kN;)
            if (rb.push(i))
                i++;
    });
    std::uint32_t expect = 0, v;
    bool in_order = true;
    while (expect < kN)
        if (rb.pop(v)) {
            if (v != expect)
                in_order = false;
            expect++;
        }
    producer.join();
    CHECK(in_order);
}

static void test_mac_constexpr()
{
    constexpr auto m = eth::MacAddress::parse("01:1B:19:00:00:00");
    static_assert(m.valid(), "parsed at compile time");
    static_assert(m.multicast(), "PTP address is multicast");
    static_assert(!eth::MacAddress::parse("01:1B:19:00:00").valid(), "too short");
    static_assert(!eth::MacAddress::parse("zz:1B:19:00:00:00").valid(), "bad hex");
    CHECK(eth::kPtpPdelay == eth::MacAddress(0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E));
    CHECK(eth::MacAddress::parse("02-00-5e-10-20-30").locallyAdministered());

    std::uint8_t buf[64];
    auto n = eth::FrameBuilder(MutBytes(buf))
                 .dst(eth::kPtpPrimary)
                 .src(eth::MacAddress(0x02, 0, 0, 0, 0, 1))
                 .vlan(5, 7)
                 .type(EP_ETHERTYPE_PTP)
                 .payload(Bytes());
    CHECK(n.ok());
    CHECK_EQ(n.value(), 60);
    CHECK_EQ(buf[14] >> 5, 7);
    std::uint8_t tiny[10];
    CHECK(eth::FrameBuilder(MutBytes(tiny)).payload(Bytes()).status() == Status::Overflow);
}

int main()
{
    RUN(test_result_and_span);
    RUN(test_ring_buffer_basic);
    RUN(test_ring_buffer_threads);
    RUN(test_mac_constexpr);
    TEST_MAIN_END();
}
