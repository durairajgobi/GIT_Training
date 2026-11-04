// SPDX-License-Identifier: MIT
#include "blemgr/reconnect_policy.hpp"
#include "test_util.hpp"

using namespace blemgr;
using namespace std::chrono_literals;

TEST(exponential_without_jitter) {
    ReconnectConfig c;
    c.initialDelay = 100ms;
    c.maxDelay = 1000ms;
    c.multiplier = 2.0;
    c.jitter = 0;
    ReconnectPolicy p(c);
    long long expect[] = {100, 200, 400, 800, 1000, 1000};
    for (long long e : expect) CHECK_EQ(p.next()->count(), e);
    CHECK_EQ(p.attempts(), 6u);
    p.reset();
    CHECK_EQ(p.next()->count(), 100);
}

TEST(jitter_stays_in_bounds) {
    ReconnectConfig c;
    c.initialDelay = 1000ms;
    c.maxDelay = 1000ms;
    c.jitter = 0.25;
    ReconnectPolicy p(c, 1234);
    bool varied = false;
    long long first = p.next()->count();
    for (int i = 0; i < 200; ++i) {
        long long v = p.next()->count();
        CHECK(v >= 750 && v <= 1250);
        varied |= v != first;
    }
    CHECK(varied);
}

TEST(max_attempts_exhausts) {
    ReconnectConfig c;
    c.maxAttempts = 2;
    c.jitter = 0;
    ReconnectPolicy p(c);
    CHECK(p.next().has_value());
    CHECK(p.next().has_value());
    CHECK(!p.next().has_value());
}

TEST(sanitises_config) {
    ReconnectConfig c;
    c.initialDelay = 500ms;
    c.maxDelay = 10ms;   // < initial
    c.multiplier = 0.5;  // < 1
    c.jitter = 0;
    ReconnectPolicy p(c);
    CHECK_EQ(p.next()->count(), 500);
    CHECK_EQ(p.next()->count(), 500);
}

TEST_MAIN()
