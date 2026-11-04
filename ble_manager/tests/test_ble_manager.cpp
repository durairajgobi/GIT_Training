// SPDX-License-Identifier: MIT
// BleManager policy tests against the scriptable fake backend in virtual time.
#include <memory>

#include "blemgr/log.hpp"
#include "fakes.hpp"
#include "test_util.hpp"

using namespace blemgr;
using namespace std::chrono_literals;

namespace {

const std::string A = "AA:BB:CC:DD:EE:01";
const std::string B = "AA:BB:CC:DD:EE:02";
const std::string C = "AA:BB:CC:DD:EE:03";
const CharRef kHrm{"0000180d-0000-1000-8000-00805f9b34fb", "00002a37-0000-1000-8000-00805f9b34fb"};

ManagerConfig defaultCfg() {
    ManagerConfig c;
    c.connectTimeout = 5000ms;
    c.maxConnections = 2;
    c.staleDeviceTimeout = 10000ms;
    c.reconnect.initialDelay = 1000ms;
    c.reconnect.maxDelay = 8000ms;
    c.reconnect.jitter = 0;
    return c;
}

struct Fx {
    fake::Scheduler s;
    fake::Backend b{s};
    fake::Listener l;
    std::unique_ptr<BleManager> m;

    explicit Fx(ManagerConfig cfg = defaultCfg()) {
        setLogLevel(LogLevel::Error);
        m = std::make_unique<BleManager>(b, s, cfg);
        m->addListener(&l);
        Status st{Error::Failed};
        m->start([&](const Status& x) { st = x; });
        s.runUntilIdle();
        CHECK(st.ok());
        GattServiceInfo hr{kHrm.service, true, {{kHrm.characteristic, {"notify"}}}};
        b.services[A] = {hr};
        b.services[B] = {hr};
    }
    ConnState state(const std::string& a) {
        auto d = m->device(a);
        return d ? d->state : ConnState::Disconnected;
    }
    void connectReady(const std::string& a, ConnectOptions o = {}) {
        o.subscriptions.push_back({"180d", "2a37"});
        m->connect(a, o);
        s.runUntilIdle();
        b.completeConnect(a);
        s.runUntilIdle();
    }
};

}  // namespace

TEST(connect_happy_path) {
    Fx f;
    ConnectOptions o;
    o.subscriptions.push_back({"180D", "2A37"});
    Status accepted{Error::Failed};
    f.m->connect("aa:bb:cc:dd:ee:01", o, [&](const Status& s) { accepted = s; });
    f.s.runUntilIdle();
    CHECK(accepted.ok());
    CHECK(f.state(A) == ConnState::Connecting);
    CHECK_EQ(f.b.connects.size(), size_t(1));

    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Ready);
    CHECK_EQ(f.l.servicesEvents, 1);
    CHECK_EQ(f.b.notifyOn.size(), size_t(1));
    CHECK_EQ(f.b.notifyOn[0], A + "|" + kHrm.characteristic);
    CHECK_EQ(f.s.pendingTimers(), size_t(1));  // only the prune timer remains

    // Expected transition sequence.
    std::vector<ConnState> seq;
    for (auto& [a, s] : f.l.states)
        if (a == A) seq.push_back(s);
    CHECK(seq == (std::vector<ConnState>{ConnState::Queued, ConnState::Connecting,
                                         ConnState::Discovering, ConnState::Ready}));
}

TEST(rejects_bad_arguments) {
    Fx f;
    Status s1, s2;
    f.m->connect("not-an-address", {}, [&](const Status& s) { s1 = s; });
    ConnectOptions o;
    o.subscriptions.push_back({"xyz", "2a37"});
    f.m->connect(A, o, [&](const Status& s) { s2 = s; });
    f.s.runUntilIdle();
    CHECK(s1.code() == Error::InvalidArgument);
    CHECK(s2.code() == Error::InvalidArgument);
    CHECK(f.b.connects.empty());
}

TEST(scan_paused_while_connecting) {
    Fx f;
    f.m->startScan({});
    f.s.runUntilIdle();
    CHECK(f.b.discovering);
    CHECK(f.m->scanning());

    f.m->connect(A);
    f.s.runUntilIdle();
    CHECK(!f.b.discovering);
    CHECK_EQ(f.b.stopDiscoveryCalls, 1);

    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.b.discovering);
    CHECK_EQ(f.b.startDiscoveryCalls, 2);

    f.m->stopScan();
    f.s.runUntilIdle();
    CHECK(!f.b.discovering);
    CHECK(!f.m->scanning());
}

TEST(scan_results_are_filtered_host_side) {
    Fx f;
    ScanFilter flt;
    flt.namePrefix = "HRM";
    flt.serviceUuids = {"180d"};
    f.m->startScan(flt);
    f.s.runUntilIdle();
    CHECK_EQ(f.b.lastFilter.serviceUuids.size(), size_t(1));  // pushed to controller, normalised
    CHECK_EQ(f.b.lastFilter.serviceUuids[0], kHrm.service);

    f.b.advertise(A, -40, "HRM-1", {kHrm.service});
    f.b.advertise(B, -40, "Other", {kHrm.service});
    f.b.advertise(C, -40, "HRM-2", {});
    f.s.runUntilIdle();
    CHECK_EQ(f.l.scanResults.size(), size_t(1));
    CHECK_EQ(f.l.scanResults[0], A);
    CHECK_EQ(f.m->devices().size(), size_t(3));  // registry still tracks everything
}

TEST(scan_duration_expires) {
    Fx f;
    f.m->startScan({}, 3000ms);
    f.s.runUntilIdle();
    CHECK(f.b.discovering);
    f.s.advance(3000ms);
    CHECK(!f.b.discovering);
}

TEST(connect_timeout_retries_with_backoff) {
    Fx f;
    f.m->connect(A);
    f.s.runUntilIdle();
    auto stale = f.b.pendingConnect[A];

    f.s.advance(5000ms);
    CHECK(f.state(A) == ConnState::WaitingRetry);
    CHECK_EQ(f.b.disconnects.size(), size_t(1));  // pending connect cancelled

    stale({});  // late reply of the superseded attempt must be ignored
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::WaitingRetry);

    f.s.advance(999ms);
    CHECK_EQ(f.b.connects.size(), size_t(1));
    f.s.advance(1ms);
    CHECK_EQ(f.b.connects.size(), size_t(2));
    CHECK(f.state(A) == ConnState::Connecting);

    f.s.advance(5000ms);  // second timeout -> 2 s back-off
    CHECK(f.state(A) == ConnState::WaitingRetry);
    f.s.advance(1999ms);
    CHECK_EQ(f.b.connects.size(), size_t(2));
    f.s.advance(1ms);
    CHECK_EQ(f.b.connects.size(), size_t(3));
    CHECK_EQ(f.m->device(A)->reconnectAttempts, 2u);

    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Ready);
    CHECK_EQ(f.m->device(A)->reconnectAttempts, 0u);  // reset on success
}

TEST(link_loss_reconnects_and_restores_subscriptions) {
    Fx f;
    f.connectReady(A);
    CHECK(f.state(A) == ConnState::Ready);
    CHECK_EQ(f.b.notifyOn.size(), size_t(1));

    f.b.dropLink(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::WaitingRetry);
    f.s.advance(1000ms);
    CHECK(f.state(A) == ConnState::Connecting);
    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Ready);
    CHECK_EQ(f.b.notifyOn.size(), size_t(2));
}

TEST(user_disconnect_stops_reconnect) {
    Fx f;
    f.connectReady(A);
    Status st{Error::Failed};
    f.m->disconnect(A, [&](const Status& s) { st = s; });
    f.s.runUntilIdle();
    CHECK(st.ok());
    CHECK(f.state(A) == ConnState::Disconnected);
    CHECK(!f.m->device(A)->managed);
    f.s.advance(60000ms);
    CHECK_EQ(f.b.connects.size(), size_t(1));
}

TEST(disconnect_while_waiting_cancels_retry) {
    Fx f;
    f.m->connect(A);
    f.s.runUntilIdle();
    f.b.completeConnect(A, {Error::Failed, "le-connection-abort-by-local"});
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::WaitingRetry);
    f.m->disconnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Disconnected);
    f.s.advance(60000ms);
    CHECK_EQ(f.b.connects.size(), size_t(1));
}

TEST(unknown_device_waits_for_advertisement) {
    Fx f;
    f.b.known = false;
    f.m->connect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::WaitingAdvert);
    CHECK(f.b.discovering);  // background scan, unfiltered
    CHECK(f.b.lastFilter.serviceUuids.empty());

    f.s.advance(60000ms);
    CHECK_EQ(f.b.connects.size(), size_t(1));  // no blind retries

    f.b.known = true;
    f.b.advertise(B, -70);  // someone else
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::WaitingAdvert);
    f.b.advertise(A, -70);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Connecting);
    CHECK(!f.b.discovering);  // paused for the connection attempt
    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Ready);
    CHECK(!f.b.discovering);  // background scan no longer needed
}

TEST(connection_attempts_are_serialized) {
    Fx f;
    f.m->connect(A);
    f.m->connect(B);
    f.s.runUntilIdle();
    CHECK_EQ(f.b.connects.size(), size_t(1));
    CHECK(f.state(B) == ConnState::Queued);
    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK_EQ(f.b.connects.size(), size_t(2));
    CHECK_EQ(f.b.connects[1], B);
}

TEST(max_connections_respected) {
    auto cfg = defaultCfg();
    cfg.maxConnections = 1;
    Fx f(cfg);
    f.m->connect(A);
    f.m->connect(B);
    f.s.runUntilIdle();
    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(A) == ConnState::Ready);
    CHECK(f.state(B) == ConnState::Queued);
    CHECK_EQ(f.b.connects.size(), size_t(1));

    f.m->disconnect(A);
    f.s.runUntilIdle();
    CHECK(f.state(B) == ConnState::Connecting);
}

TEST(retry_budget_exhausted) {
    auto cfg = defaultCfg();
    cfg.reconnect.maxAttempts = 2;
    cfg.staleDeviceTimeout = 600000ms;  // keep the record for inspection
    Fx f(cfg);
    f.m->connect(A);
    for (int i = 0; i < 3; ++i) {
        f.s.runUntilIdle();
        f.b.completeConnect(A, {Error::Failed, "page timeout"});
        f.s.advance(10000ms);
    }
    CHECK(f.state(A) == ConnState::Disconnected);
    CHECK(f.m->device(A).has_value() && !f.m->device(A)->managed);
    CHECK_EQ(f.b.connects.size(), size_t(3));
}

TEST(non_managed_link_is_not_restored) {
    Fx f;
    ConnectOptions o;
    o.autoReconnect = false;
    f.connectReady(A, o);
    CHECK(f.state(A) == ConnState::Ready);
    f.b.dropLink(A);
    f.s.advance(60000ms);
    CHECK(f.state(A) == ConnState::Disconnected);
    CHECK_EQ(f.b.connects.size(), size_t(1));
}

TEST(gatt_operations_need_ready_link) {
    Fx f;
    Status rs;
    f.m->read(A, {"180d", "2a37"}, [&](const Status& s, const Bytes&) { rs = s; });
    f.s.runUntilIdle();
    CHECK(rs.code() == Error::NotConnected);

    f.connectReady(A);
    Bytes got;
    f.m->read(A, {"180d", "2a37"}, [&](const Status& s, const Bytes& v) {
        rs = s;
        got = v;
    });
    Status ws{Error::Failed};
    f.m->write(A, {"180d", "2a39"}, {1, 2}, WriteType::WithResponse, [&](const Status& s) { ws = s; });
    f.s.runUntilIdle();
    CHECK(rs.ok());
    CHECK(got == Bytes{0x2a});
    CHECK(ws.ok());
    CHECK_EQ(f.b.writes.size(), size_t(1));

    Status bad;
    f.m->write(A, {"zz", "2a39"}, {}, WriteType::WithResponse, [&](const Status& s) { bad = s; });
    f.s.runUntilIdle();
    CHECK(bad.code() == Error::InvalidArgument);
}

TEST(subscribe_and_unsubscribe_runtime) {
    Fx f;
    f.m->connect(A);
    f.s.runUntilIdle();
    f.b.completeConnect(A);
    f.s.runUntilIdle();
    CHECK(f.b.notifyOn.empty());

    f.m->subscribe(A, {"180f", "2a19"});
    f.s.runUntilIdle();
    CHECK_EQ(f.b.notifyOn.size(), size_t(1));
    CHECK_EQ(f.m->device(A)->subscriptions.size(), size_t(1));

    f.m->unsubscribe(A, {"180f", "2a19"});
    f.s.runUntilIdle();
    CHECK_EQ(f.b.notifyOff.size(), size_t(1));
    CHECK(f.m->device(A)->subscriptions.empty());
}

TEST(notifications_are_routed) {
    Fx f;
    f.connectReady(A);
    f.b.ev->onNotification(A, kHrm, {0x06, 0x48});
    CHECK_EQ(f.l.notifications.size(), size_t(1));
    CHECK(f.l.notifications[0].second == (Bytes{0x06, 0x48}));
}

TEST(pairing_after_first_connection) {
    Fx f;
    ConnectOptions o;
    o.pair = true;
    f.connectReady(A, o);
    CHECK_EQ(f.b.pairs.size(), size_t(1));
    CHECK_EQ(f.l.pairingEvents, 1);
}

TEST(adapter_power_loss_and_recovery) {
    Fx f;
    f.connectReady(A);
    f.m->connect(B);  // stays queued while the adapter is off
    f.b.ev->onAdapterChanged(AdapterState::Off);
    f.b.dropLink(A);
    f.s.runUntilIdle();  // manager asks for power, fake turns adapter back on
    CHECK(f.m->adapterState() == AdapterState::On);
    f.s.advance(1000ms);
    CHECK(f.state(A) == ConnState::Connecting || f.state(B) == ConnState::Connecting);
}

TEST(incoming_connection_tracked) {
    Fx f;
    f.b.ev->onConnectionChanged(C, true);
    CHECK(f.state(C) == ConnState::Discovering);
    f.b.ev->onServicesResolved(C, {});
    CHECK(f.state(C) == ConnState::Ready);
    f.b.ev->onConnectionChanged(C, false);
    CHECK(f.state(C) == ConnState::Disconnected);
}

TEST(stale_devices_pruned) {
    Fx f;
    f.b.advertise(C, -80);
    f.s.runUntilIdle();
    CHECK(f.m->device(C).has_value());
    f.s.advance(16000ms);
    CHECK(!f.m->device(C).has_value());
}

TEST(stop_tears_everything_down) {
    Fx f;
    f.connectReady(A);
    f.m->startScan({});
    f.s.runUntilIdle();
    bool done = false;
    f.m->stop([&](const Status&) { done = true; });
    f.s.runUntilIdle();
    CHECK(done);
    CHECK_EQ(f.b.disconnects.size(), size_t(1));
    CHECK(f.state(A) == ConnState::Disconnected);
    CHECK(f.b.ev == nullptr);
    f.s.advance(60000ms);
    CHECK_EQ(f.b.connects.size(), size_t(1));
}

TEST_MAIN()
