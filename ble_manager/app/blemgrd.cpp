// SPDX-License-Identifier: MIT
// blemgrd - BLE manager daemon for embedded Linux (BlueZ).
//
//   blemgrd [-c /etc/blemgr/blemgrd.conf] [-i] [-v] [-a hci0] [--bus ADDRESS]
//
// Keeps the configured devices connected (auto-reconnect, notifications),
// optionally exposes a GATT server + advertisement, and offers an interactive
// shell on stdin with -i.
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <future>
#include <iostream>
#include <string>

#include "blemgr/ble_manager.hpp"
#include "blemgr/bluez_backend.hpp"
#include "blemgr/config.hpp"
#include "blemgr/glib_scheduler.hpp"
#include "blemgr/log.hpp"
#include "shell.hpp"

using namespace blemgr;

namespace {

constexpr const char* kDefaultConfig = "/etc/blemgr/blemgrd.conf";
// Demo peripheral: vendor telemetry service.
constexpr const char* kTelemetrySvc = "a0b40001-926d-4d61-98df-8c5c62ee53b3";
constexpr const char* kStatusChr = "a0b40002-926d-4d61-98df-8c5c62ee53b3";
constexpr const char* kCommandChr = "a0b40003-926d-4d61-98df-8c5c62ee53b3";

/// Minimal sd_notify(3) so the unit can be Type=notify without libsystemd.
void sdNotify(const char* state) {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || (path[0] != '/' && path[0] != '@')) return;
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    sockaddr_un sa{};
    sa.sun_family = AF_UNIX;
    size_t len = std::min(std::strlen(path), sizeof(sa.sun_path) - 1);
    std::memcpy(sa.sun_path, path, len);
    if (sa.sun_path[0] == '@') sa.sun_path[0] = '\0';  // abstract namespace
    sendto(fd, state, std::strlen(state), MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&sa),
           static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + len));
    close(fd);
}

class LogListener : public IBleListener {
public:
    void onAdapterStateChanged(AdapterState s) override { BLE_LOGI("[event] adapter %s", toString(s)); }
    void onScanResult(const DeviceInfo& d) override {
        BLE_LOGI("[scan] %s rssi=%d name=\"%s\" uuids=%zu", d.address.c_str(), d.rssi.value_or(0),
                 d.name.c_str(), d.serviceUuids.size());
    }
    void onConnectionStateChanged(const std::string& a, ConnState s, const Status& r) override {
        BLE_LOGI("[event] %s %s%s%s", a.c_str(), toString(s), r.ok() ? "" : " ",
                 r.ok() ? "" : r.toString().c_str());
    }
    void onServicesDiscovered(const std::string& a, const std::vector<GattServiceInfo>& s) override {
        BLE_LOGI("[event] %s %zu services", a.c_str(), s.size());
    }
    void onNotification(const std::string& a, const CharRef& c, const Bytes& v) override {
        BLE_LOGI("[notify] %s %s %s", a.c_str(), c.characteristic.c_str(), toHex(v).c_str());
    }
    void onPairingResult(const std::string& a, const Status& s) override {
        BLE_LOGI("[event] %s pairing %s", a.c_str(), s.toString().c_str());
    }
};

std::vector<LocalService> demoServices() {
    LocalService dis;
    dis.uuid = "180a";  // Device Information
    auto str = [](const char* s) { return Bytes(s, s + std::strlen(s)); };
    dis.characteristics.push_back({"2a29", kPropRead, str("TCU"), nullptr, nullptr});
    dis.characteristics.push_back({"2a24", kPropRead, str("blemgrd"), nullptr, nullptr});
    dis.characteristics.push_back({"2a26", kPropRead, str("1.0.0"), nullptr, nullptr});

    LocalService tel;
    tel.uuid = kTelemetrySvc;
    tel.characteristics.push_back({kStatusChr, kPropRead | kPropNotify, Bytes(4, 0), nullptr, nullptr});
    LocalCharacteristic cmd;
    cmd.uuid = kCommandChr;
    cmd.properties = kPropWrite | kPropWriteNoResponse;
    cmd.onWrite = [](const Bytes& v) -> Status {
        if (v.empty()) return {Error::InvalidArgument, "empty command"};
        BLE_LOGI("[gatt] command received: %s", toHex(v).c_str());
        return {};
    };
    tel.characteristics.push_back(std::move(cmd));
    return {dis, tel};
}

template <class F>
Status waitFor(F&& start) {
    auto p = std::make_shared<std::promise<Status>>();
    auto fut = p->get_future();
    start([p](const Status& s) { p->set_value(s); });
    if (fut.wait_for(std::chrono::seconds(30)) != std::future_status::ready)
        return {Error::Timeout, "no answer from event loop"};
    return fut.get();
}

void usage() {
    std::fprintf(stderr,
                 "usage: blemgrd [-c config] [-i] [-v] [-a adapter] [--bus dbus-address]\n"
                 "  -c FILE   configuration (default %s if present)\n"
                 "  -i        interactive shell on stdin\n"
                 "  -v        debug logging\n"
                 "  -a NAME   adapter, overrides config (e.g. hci1)\n"
                 "  --bus A   D-Bus address instead of the system bus\n",
                 kDefaultConfig);
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath;
    std::string adapter, bus;
    bool interactive = false, verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-c") configPath = next();
        else if (a == "-i") interactive = true;
        else if (a == "-v") verbose = true;
        else if (a == "-a") adapter = next();
        else if (a == "--bus") bus = next();
        else {
            usage();
            return a == "-h" || a == "--help" ? 0 : 2;
        }
    }

    DaemonConfig cfg;
    std::string err;
    if (!configPath.empty()) {
        auto c = loadConfigFile(configPath, &err);
        if (!c) {
            std::fprintf(stderr, "blemgrd: %s\n", err.c_str());
            return 2;
        }
        cfg = *c;
    } else if (access(kDefaultConfig, R_OK) == 0) {
        auto c = loadConfigFile(kDefaultConfig, &err);
        if (!c) {
            std::fprintf(stderr, "blemgrd: %s\n", err.c_str());
            return 2;
        }
        cfg = *c;
    }
    if (!adapter.empty()) cfg.bluez.adapter = adapter;
    if (!bus.empty()) cfg.bluez.busAddress = bus;
    setLogLevel(verbose ? LogLevel::Debug : cfg.logLevel);

    // Signals are consumed synchronously through a signalfd.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);  // inherited by the loop thread
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) {
        std::perror("signalfd");
        return 1;
    }

    GlibScheduler sched;
    sched.start();
    BluezBackend backend(cfg.bluez);
    BleManager mgr(backend, sched, cfg.manager);
    LogListener logListener;
    mgr.addListener(&logListener);

    Status st = waitFor([&](StatusCallback cb) { mgr.start(cb); });
    if (!st.ok()) {
        BLE_LOGE("startup failed: %s", st.toString().c_str());
        sched.stop();
        return 1;
    }

    for (const auto& d : cfg.devices) {
        mgr.connect(d.address, d.options, [addr = d.address](const Status& s) {
            if (!s.ok()) BLE_LOGE("managed device %s: %s", addr.c_str(), s.toString().c_str());
        });
    }

    if (cfg.peripheral.enabled) {
        mgr.publishGattServer(demoServices(), [](const Status& s) {
            if (!s.ok()) BLE_LOGE("GATT server: %s", s.toString().c_str());
        });
        AdvertisementData adv;
        adv.localName = cfg.peripheral.name;
        adv.serviceUuids = {kTelemetrySvc};
        adv.includeTxPower = true;
        mgr.startAdvertising(adv, [](const Status& s) {
            if (!s.ok()) BLE_LOGE("advertising: %s", s.toString().c_str());
        });
    }

    sdNotify("READY=1\nSTATUS=running");
    BLE_LOGI("blemgrd running (adapter %s, %zu managed devices%s)", cfg.bluez.adapter.c_str(),
             cfg.devices.size(), cfg.peripheral.enabled ? ", peripheral on" : "");
    if (interactive) {
        blemgrd::printHelp();
        std::printf("> ");
        std::fflush(stdout);
    }

    const auto started = std::chrono::steady_clock::now();
    const int tickMs = static_cast<int>(cfg.peripheral.notifyIntervalMs);
    auto nextTick = started + std::chrono::milliseconds(tickMs);
    bool running = true;
    while (running) {
        pollfd fds[2] = {{sfd, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        int timeout = static_cast<int>(std::max<long long>(
            0, std::chrono::duration_cast<std::chrono::milliseconds>(nextTick -
                                                                     std::chrono::steady_clock::now())
                   .count()));
        int n = poll(fds, interactive ? 2 : 1, timeout);
        if (n < 0 && errno != EINTR) break;

        if (fds[0].revents & POLLIN) {
            signalfd_siginfo si{};
            if (::read(sfd, &si, sizeof si) == sizeof si) {
                if (si.ssi_signo == SIGHUP) {
                    BLE_LOGI("SIGHUP: devices:");
                    for (const auto& d : mgr.devices())
                        BLE_LOGI("  %s %s", d.info.address.c_str(), toString(d.state));
                } else {
                    BLE_LOGI("signal %u, shutting down", si.ssi_signo);
                    running = false;
                }
            }
        }
        if (interactive && (fds[1].revents & (POLLIN | POLLHUP))) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                running = false;
            } else {
                running = blemgrd::runCommand(mgr, line);
                if (running) {
                    std::printf("> ");
                    std::fflush(stdout);
                }
            }
        }
        if (std::chrono::steady_clock::now() >= nextTick) {
            nextTick += std::chrono::milliseconds(tickMs);
            if (cfg.peripheral.enabled) {
                auto up = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::steady_clock::now() - started)
                                                    .count());
                Bytes v{static_cast<uint8_t>(up), static_cast<uint8_t>(up >> 8),
                        static_cast<uint8_t>(up >> 16), static_cast<uint8_t>(up >> 24)};
                mgr.notifyLocal({kTelemetrySvc, kStatusChr}, v);
            }
        }
    }

    sdNotify("STOPPING=1");
    waitFor([&](StatusCallback cb) { mgr.stop(cb); });
    sched.stop();
    close(sfd);
    BLE_LOGI("blemgrd stopped");
    return 0;
}
