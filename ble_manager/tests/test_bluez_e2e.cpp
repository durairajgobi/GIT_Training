// SPDX-License-Identifier: MIT
// End-to-end test: BleManager + BluezBackend + GlibScheduler talking D-Bus to
// mock_bluez on a private dbus-daemon.
//
//   test_bluez_e2e <mock_bluez> <dbus-daemon>
#include <gio/gio.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <future>
#include <map>
#include <mutex>

#include "blemgr/ble_manager.hpp"
#include "blemgr/bluez_backend.hpp"
#include "blemgr/glib_scheduler.hpp"
#include "blemgr/log.hpp"
#include "test_util.hpp"

using namespace blemgr;
using namespace std::chrono_literals;

namespace {

const char* g_mock;
const char* g_daemon;
const std::string kDev = "11:22:33:44:55:66";
const CharRef kHrm{"180d", "2a37"};
const CharRef kCtl{"180d", "2a39"};

pid_t spawn(std::vector<std::string> args) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
    return pid;
}

struct Recorder : IBleListener {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<DeviceInfo> scan;
    std::vector<ConnState> states;
    int notifications = 0;
    int pairings = 0;
    AdapterState adapter = AdapterState::Unavailable;

    void onAdapterStateChanged(AdapterState s) override { set([&] { adapter = s; }); }
    void onScanResult(const DeviceInfo& d) override { set([&] { scan.push_back(d); }); }
    void onConnectionStateChanged(const std::string& a, ConnState s, const Status&) override {
        if (a == kDev) set([&] { states.push_back(s); });
    }
    void onNotification(const std::string& a, const CharRef&, const Bytes& v) override {
        if (a == kDev && v.size() == 2 && v[0] == 0x06) set([&] { ++notifications; });
    }
    void onPairingResult(const std::string&, const Status& s) override {
        if (s.ok()) set([&] { ++pairings; });
    }

    template <class F> void set(F f) {
        {
            std::lock_guard<std::mutex> lk(mu);
            f();
        }
        cv.notify_all();
    }
    template <class P> bool waitFor(P pred, std::chrono::milliseconds t = 5000ms) {
        std::unique_lock<std::mutex> lk(mu);
        return cv.wait_for(lk, t, pred);
    }
    ConnState last() {
        std::lock_guard<std::mutex> lk(mu);
        return states.empty() ? ConnState::Disconnected : states.back();
    }
};

template <class F>
Status await(F&& start) {
    auto p = std::make_shared<std::promise<Status>>();
    auto f = p->get_future();
    start([p](const Status& s) { p->set_value(s); });
    if (f.wait_for(5s) != std::future_status::ready) return {Error::Timeout, "no completion"};
    return f.get();
}

std::map<std::string, std::string> mockReport(GDBusConnection* c) {
    std::map<std::string, std::string> out;
    GVariant* r = g_dbus_connection_call_sync(c, "org.bluez", "/mock", "com.tcu.MockControl",
                                              "GetReport", nullptr, G_VARIANT_TYPE("(a{ss})"),
                                              G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
    if (!r) return out;
    GVariantIter* it;
    g_variant_get(r, "(a{ss})", &it);
    const gchar *k, *v;
    while (g_variant_iter_loop(it, "{&s&s}", &k, &v)) out[k] = v;
    g_variant_iter_free(it);
    g_variant_unref(r);
    return out;
}

void mockCall(GDBusConnection* c, const char* method) {
    GVariant* r = g_dbus_connection_call_sync(c, "org.bluez", "/mock", "com.tcu.MockControl", method,
                                              nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
                                              nullptr);
    if (r) g_variant_unref(r);
}

}  // namespace

TEST(end_to_end_against_mock_bluez) {
    // ---- private bus + mock bluetoothd
    char tmpl[] = "/tmp/blemgr-e2e-XXXXXX";
    std::string dir = mkdtemp(tmpl);
    std::string sock = dir + "/bus";
    std::string address = "unix:path=" + sock;
    {
        std::ofstream conf(dir + "/bus.conf");
        conf << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\" "
                "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                "<busconfig><type>session</type><listen>" << address << "</listen>"
                "<auth>EXTERNAL</auth><policy context=\"default\">"
                "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                "<allow own=\"*\"/></policy></busconfig>\n";
    }
    pid_t bus = spawn({g_daemon, "--config-file=" + dir + "/bus.conf", "--nofork", "--nopidfile"});
    struct stat st;
    for (int i = 0; i < 100 && stat(sock.c_str(), &st) != 0; ++i) usleep(20000);
    CHECK(stat(sock.c_str(), &st) == 0);
    pid_t mock = spawn({g_mock, address});

    GDBusConnection* ctl = g_dbus_connection_new_for_address_sync(
        address.c_str(),
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, nullptr);
    CHECK(ctl != nullptr);
    bool owned = false;
    for (int i = 0; i < 100 && ctl && !owned; ++i) {
        GVariant* r = g_dbus_connection_call_sync(
            ctl, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "NameHasOwner", g_variant_new("(s)", "org.bluez"), G_VARIANT_TYPE("(b)"),
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        if (r) {
            g_variant_get(r, "(b)", &owned);
            g_variant_unref(r);
        }
        if (!owned) usleep(20000);
    }
    CHECK(owned);

    // ---- system under test
    if (owned) {
        BluezOptions bo;
        bo.busAddress = address;
        ManagerConfig mc;
        mc.connectTimeout = 3000ms;
        mc.reconnect.initialDelay = 200ms;
        mc.reconnect.jitter = 0;

        GlibScheduler sched;
        sched.start();
        BluezBackend backend(bo);
        BleManager mgr(backend, sched, mc);
        Recorder rec;
        mgr.addListener(&rec);

        // Adapter starts powered off; the manager must power it on.
        CHECK(await([&](StatusCallback cb) { mgr.start(cb); }).ok());
        CHECK(rec.waitFor([&] { return rec.adapter == AdapterState::On; }));

        // Scan with a controller-side UUID filter.
        ScanFilter f;
        f.serviceUuids = {"180d"};
        CHECK(await([&](StatusCallback cb) { mgr.startScan(f, {}, cb); }).ok());
        CHECK(rec.waitFor([&] { return !rec.scan.empty(); }));
        if (!rec.scan.empty()) {
            const DeviceInfo& d = rec.scan.front();
            CHECK_EQ(d.address, kDev);
            CHECK_EQ(d.name, std::string("Mock-HRM"));
            CHECK(d.rssi && *d.rssi == -55);
            CHECK(d.addressType == AddressType::Random);
            CHECK(d.manufacturerData.count(0xffff) == 1);
        }

        // Connect with a persistent subscription and bonding.
        ConnectOptions co;
        co.subscriptions = {kHrm};
        co.pair = true;
        CHECK(await([&](StatusCallback cb) { mgr.connect(kDev, co, cb); }).ok());
        CHECK(rec.waitFor([&] { return !rec.states.empty() && rec.states.back() == ConnState::Ready; }));
        auto snap = mgr.device(kDev);
        CHECK(snap && snap->services.size() == 1 && snap->services[0].characteristics.size() == 2);
        CHECK(rec.waitFor([&] { return rec.notifications >= 3; }));
        CHECK(rec.waitFor([&] { return rec.pairings == 1; }));

        // GATT client read / write.
        auto p = std::make_shared<std::promise<Bytes>>();
        mgr.read(kDev, kHrm, [p](const Status& s, const Bytes& v) { p->set_value(s.ok() ? v : Bytes{}); });
        auto fut = p->get_future();
        CHECK(fut.wait_for(5s) == std::future_status::ready && fut.get() == (Bytes{0x06, 0x48}));
        CHECK(await([&](StatusCallback cb) {
                  mgr.write(kDev, kCtl, {0xde, 0xad}, WriteType::WithoutResponse, cb);
              }).ok());
        auto rep = mockReport(ctl);
        CHECK_EQ(rep["last_write"], std::string("dead"));
        CHECK_EQ(rep["last_write_type"], std::string("command"));
        CHECK_EQ(rep["agent"], std::string("NoInputNoOutput"));
        CHECK_EQ(rep["filter_transport"], std::string("le"));

        // Link loss -> automatic reconnect -> notifications resume.
        mockCall(ctl, "DropLink");
        CHECK(rec.waitFor([&] {
            return std::find(rec.states.begin(), rec.states.end(), ConnState::WaitingRetry) != rec.states.end();
        }));
        int before;
        {
            std::lock_guard<std::mutex> lk(rec.mu);
            before = rec.notifications;
        }
        CHECK(rec.waitFor([&] { return rec.states.back() == ConnState::Ready; }));
        CHECK(rec.waitFor([&] { return rec.notifications >= before + 3; }));
        CHECK_EQ(mockReport(ctl)["connects"], std::string("2"));

        // Peripheral role: GATT server + advertisement, exercised by the mock acting as BlueZ.
        LocalService dis{"180a", true, {}};
        dis.characteristics.push_back({"2a29", kPropRead, {'T', 'C', 'U'}, nullptr, nullptr});
        LocalService tel{"a0b40001-926d-4d61-98df-8c5c62ee53b3", true, {}};
        std::atomic<int> writes{0};
        LocalCharacteristic cmd;
        cmd.uuid = "a0b40003-926d-4d61-98df-8c5c62ee53b3";
        cmd.properties = kPropWrite;
        cmd.onWrite = [&](const Bytes& v) -> Status {
            if (v == Bytes{0x01, 0x02}) ++writes;
            return {};
        };
        tel.characteristics.push_back(cmd);
        CHECK(await([&](StatusCallback cb) { mgr.publishGattServer({dis, tel}, cb); }).ok());
        rep = mockReport(ctl);
        CHECK_EQ(rep["app_objects"], std::string("4"));  // 2 services + 2 characteristics
        CHECK_EQ(rep["app_read"], std::string("544355"));  // "TCU"
        CHECK_EQ(rep["app_write"], std::string("ok"));
        CHECK_EQ(writes.load(), 1);

        AdvertisementData adv;
        adv.localName = "TCU-E2E";
        CHECK(await([&](StatusCallback cb) { mgr.startAdvertising(adv, cb); }).ok());
        rep = mockReport(ctl);
        CHECK_EQ(rep["adv_name"], std::string("TCU-E2E"));
        CHECK_EQ(rep["adv_type"], std::string("peripheral"));

        // User disconnect is final.
        CHECK(await([&](StatusCallback cb) { mgr.disconnect(kDev, cb); }).ok());
        CHECK(rec.waitFor([&] { return rec.states.back() == ConnState::Disconnected; }));
        usleep(500000);
        CHECK(rec.last() == ConnState::Disconnected);

        CHECK(await([&](StatusCallback cb) { mgr.stop(cb); }).ok());
        sched.stop();
    }

    if (ctl) g_object_unref(ctl);
    kill(mock, SIGTERM);
    kill(bus, SIGTERM);
    waitpid(mock, nullptr, 0);
    waitpid(bus, nullptr, 0);
    std::remove((dir + "/bus.conf").c_str());
    std::remove(sock.c_str());
    rmdir(dir.c_str());
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: test_bluez_e2e <mock_bluez> <dbus-daemon>\n");
        return 2;
    }
    g_mock = argv[1];
    g_daemon = argv[2];
    setLogLevel(std::getenv("BLEMGR_E2E_VERBOSE") ? LogLevel::Debug : LogLevel::Warn);
    return t::runAll();
}
