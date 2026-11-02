#include "epp/ptp.hpp"
#include "epp/tsn.hpp"
#include "ep_test.h"

#include <cmath>

using namespace epp;

// ---- compile-time schedule checks: these fail the build, not a test ----
constexpr auto kGcl = tsn::makeGcl({
    {0x80, 100000},  // TC7: motion control, exclusive
    {0x40, 50000},   // TC6: audio/video (CBS shaped)
    {0x3F, 350000},  // best effort
});
static_assert(kGcl.cycleTime() == 500000, "500 us cycle");
static_assert(kGcl.openTime(7) == 100000, "");
static_assert(kGcl.maxGateClosed(7) == 400000, "worst-case wait for TC7");
static_assert(kGcl.maxGateClosed(0) == 150000, "");
static_assert(kGcl.shareppm(7) == 200000, "20 % of the link");
static_assert(tsn::maxFrameInWindow(100000, 1000000000ull) >= 1514, "a full frame fits the TC7 window at 1G");
static_assert(tsn::maxFrameInWindow(10000, 100000000ull) == 101, "10 us at 100M: 125 B - overhead");

constexpr ptp::Timestamp kT(1700000000ull, 999999999u);
static_assert((kT + 1).seconds() == 1700000001ull, "carry into seconds");
static_assert((kT + 1).nanoseconds() == 0, "");
static_assert((kT + 1) - kT == 1, "");

static void test_timestamp_codec()
{
    std::uint8_t b[10];
    kT.pack(b);
    CHECK(ptp::Timestamp::unpack(b) == kT);
    CHECK(std::fabs(ptp::correctionToNs(ptp::toCorrection(-250)) + 250.0) < 1e-12);
}

static void test_median_filter()
{
    ptp::MedianFilter<std::int64_t, 5> f;
    CHECK_EQ(f.push(1000), 1000);
    CHECK_EQ(f.push(1010), 1005);
    CHECK_EQ(f.push(990), 1000);
    CHECK_EQ(f.push(50000), 1005); // queueing spike rejected
    CHECK_EQ(f.push(1002), 1002);
    CHECK_EQ(f.push(1001), 1002);  // window slides
}

static void test_servo_wrapper_tracks_drift()
{
    ptp::PiServo servo;
    double freq = 0;           // applied correction
    const double err = 20000;  // 20 ppm fast oscillator
    double offset = 5000;      // ns
    std::uint64_t t = 0;
    for (int i = 0; i < 60; i++) {
        auto o = servo.sample(static_cast<std::int64_t>(std::llround(offset)), t);
        if (o.state == EP_SERVO_JUMP)
            offset = 0;
        freq = o.ppb;
        t += 1000000000ull;
        offset += (err + freq); // 1 s at (err + correction) ppb
    }
    CHECK(std::fabs(offset) < 5.0);
    CHECK(std::fabs(freq + err) < 5.0);
}

static void test_tas_wrapper()
{
    tsn::TimeAwareShaper tas(kGcl, 0, 1000000000ull);
    CHECK(tas.initStatus() == Status::Ok);
    CHECK_EQ(tas.gates(0), 0x80);
    CHECK_EQ(tas.gates(120000), 0x40);
    CHECK(tas.canTransmit(10000, 7, 1514));
    CHECK(!tas.canTransmit(95000, 7, 1514));
    CHECK_EQ(tas.nextTransmit(95000, 7, 1514), 500000);
    CHECK_EQ(tas.nextTransmit(0, 0, 64), 150000);
}

static void test_cbs_and_frer_wrappers()
{
    tsn::CreditBasedShaper a(75000000ull, 1000000000ull); // class A 75 Mbit/s on 1G
    CHECK(a.initStatus() == Status::Ok);
    CHECK_EQ(a.hiCredit(), 1522 * 8 * 75 / 1000);
    CHECK(a.loCredit() < 0);
    tsn::CreditBasedShaper bad(2000000000ull, 1000000000ull);
    CHECK(bad.initStatus() == Status::Param);

    tsn::SequenceRecovery rec(16);
    int passed = 0;
    const std::uint16_t path_a[] = {1, 2, 3, 5, 6};
    const std::uint16_t path_b[] = {1, 2, 4, 5, 6};
    for (int i = 0; i < 5; i++) {
        passed += rec.accept(path_a[i]);
        passed += rec.accept(path_b[i]);
    }
    CHECK_EQ(passed, 6); // 1..6 exactly once despite a loss on each path
}

int main()
{
    RUN(test_timestamp_codec);
    RUN(test_median_filter);
    RUN(test_servo_wrapper_tracks_drift);
    RUN(test_tas_wrapper);
    RUN(test_cbs_and_frer_wrappers);
    TEST_MAIN_END();
}
