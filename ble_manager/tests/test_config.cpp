// SPDX-License-Identifier: MIT
#include <sstream>

#include "blemgr/config.hpp"
#include "test_util.hpp"

using namespace blemgr;

static std::optional<DaemonConfig> parse(const std::string& s, std::string* err = nullptr) {
    std::istringstream in(s);
    std::string e;
    auto c = parseConfig(in, &e);
    if (err) *err = e;
    return c;
}

TEST(full_config) {
    auto c = parse(R"(
# comment
[general]
log_level = debug
[adapter]
name = hci1
power_on = no
[agent]
capability = DisplayYesNo
auto_accept = false
passkey = 123456
[connection]
timeout_ms = 8000
max_connections = 2
pause_scan_during_connect = off
stale_device_timeout_ms = 30000
[reconnect]
initial_delay_ms = 250
max_delay_ms = 4000
multiplier = 1.5
jitter = 0.1
max_attempts = 7
[peripheral]
enabled = true
name = MyUnit
notify_interval_ms = 1000
[device aa:bb:cc:dd:ee:ff]
pair = yes
subscribe = 180d/2a37   ; heart rate measurement
subscribe = 180f / 2a19
[device 11-22-33-44-55-66]
auto_reconnect = false
)");
    CHECK(c.has_value());
    if (!c) return;
    CHECK(c->logLevel == LogLevel::Debug);
    CHECK_EQ(c->bluez.adapter, std::string("hci1"));
    CHECK(!c->manager.powerOnStart);
    CHECK_EQ(c->bluez.agentCapability, std::string("DisplayYesNo"));
    CHECK(!c->bluez.agentAutoAccept);
    CHECK_EQ(c->bluez.agentPasskey, 123456u);
    CHECK_EQ(c->manager.connectTimeout.count(), 8000);
    CHECK_EQ(c->manager.maxConnections, 2u);
    CHECK(!c->manager.pauseScanDuringConnect);
    CHECK_EQ(c->manager.reconnect.initialDelay.count(), 250);
    CHECK_EQ(c->manager.reconnect.maxAttempts, 7u);
    CHECK(c->peripheral.enabled);
    CHECK_EQ(c->peripheral.name, std::string("MyUnit"));
    CHECK_EQ(c->devices.size(), size_t(2));
    CHECK_EQ(c->devices[0].address, std::string("AA:BB:CC:DD:EE:FF"));
    CHECK(c->devices[0].options.pair);
    CHECK_EQ(c->devices[0].options.subscriptions.size(), size_t(2));
    CHECK_EQ(c->devices[0].options.subscriptions[1].characteristic,
             std::string("00002a19-0000-1000-8000-00805f9b34fb"));
    CHECK(!c->devices[1].options.autoReconnect);
}

TEST(defaults_from_empty_file) {
    auto c = parse("");
    CHECK(c.has_value());
    CHECK_EQ(c->bluez.adapter, std::string("hci0"));
    CHECK(c->devices.empty());
}

TEST(errors_carry_line_numbers) {
    std::string e;
    CHECK(!parse("[adapter]\nbogus = 1\n", &e));
    CHECK_EQ(e, std::string("line 2: unknown key bogus"));
    CHECK(!parse("[nope]\n", &e));
    CHECK(!parse("[connection]\nmax_connections = 0\n", &e));
    CHECK(!parse("[connection]\ntimeout_ms = abc\n", &e));
    CHECK(!parse("[device zz]\n", &e));
    CHECK(!parse("[device AA:BB:CC:DD:EE:FF]\nsubscribe = 180d\n", &e));
    CHECK(!parse("name = x\n", &e));
    CHECK(!parse("[agent]\ncapability = Telepathy\n", &e));
    CHECK(!parse("[device AA:BB:CC:DD:EE:FF]\n[device aa:bb:cc:dd:ee:ff]\n", &e));
    CHECK(!parse("[reconnect]\ninitial_delay_ms = 500\nmax_delay_ms = 100\n", &e));
}

TEST_MAIN()
