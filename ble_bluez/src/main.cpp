// SPDX-License-Identifier: MIT
// ble-app: BLE central/peripheral tool on top of BlueZ's D-Bus API.
//
//   ble-app scan    [-t SEC] [--uuid UUID] [--rssi DBM] [--name PREFIX]
//   ble-app connect MAC [-t SEC] [--no-pair] [--stay]
//   ble-app auto    [-t SEC] [--uuid UUID] [--rssi DBM] [--name PREFIX] [--max N]
//   ble-app read    MAC UUID
//   ble-app write   MAC UUID HEX [--no-response]
//   ble-app notify  MAC UUID [-t SEC]
//   ble-app disconnect MAC
//   ble-app remove  MAC
//   ble-app serve   [--name NAME]
//
// Global options: -i hciX, --agent CAPABILITY, --interactive
#include <glib-unix.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ble/agent.hpp"
#include "ble/ble_manager.hpp"
#include "ble/dbus.hpp"
#include "ble/device.hpp"
#include "ble/gatt_client.hpp"
#include "ble/gatt_server.hpp"
#include "ble/utils.hpp"

using namespace ble;

namespace {

struct Args {
    std::string adapter = "hci0";
    std::string command;
    std::vector<std::string> positional;
    unsigned timeoutSec = 0;      // 0 = command default
    std::vector<std::string> uuids;
    std::optional<int16_t> rssi;
    std::string namePrefix;
    std::string localName = "BlueZ-CPP";
    std::string agentCap;
    bool interactive = false;
    bool pair = true;
    bool stay = false;
    bool withResponse = true;
    unsigned max = 0;
};

void usage() {
    std::cerr <<
R"(usage: ble-app [-i hciX] [--agent CAP] [--interactive] <command> [options]

Central role:
  scan    [-t SEC] [--uuid UUID]... [--rssi DBM] [--name PREFIX]
          List discoverable devices (default 10 s).
  connect MAC [-t SEC] [--no-pair] [--stay]
          Discover MAC, pair, trust, connect, print its GATT database.
  auto    [-t SEC] [--uuid UUID]... [--rssi DBM] [--name PREFIX] [--max N]
          Discover and connect to every matching device (default 30 s).
  read    MAC UUID                     Read a characteristic.
  write   MAC UUID HEX [--no-response] Write a characteristic.
  notify  MAC UUID [-t SEC]            Subscribe and print notifications.
  disconnect MAC                       Disconnect a device.
  remove  MAC                          Forget (unpair) a device.

Peripheral role:
  serve   [--name NAME]
          Register an agent, a GATT application (Battery, Device Info and an
          echo service) and an LE advertisement, then wait for centrals.

Agent capabilities: DisplayOnly DisplayYesNo KeyboardOnly NoInputNoOutput
                    KeyboardDisplay (default; serve defaults to NoInputNoOutput)
)";
}

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + s);
            return argv[++i];
        };
        if (s == "-h" || s == "--help") return false;
        else if (s == "-i" || s == "--adapter") a.adapter = next();
        else if (s == "-t" || s == "--timeout") a.timeoutSec = std::stoul(next());
        else if (s == "--uuid") a.uuids.push_back(normalizeUuid(next()));
        else if (s == "--rssi") a.rssi = static_cast<int16_t>(std::stoi(next()));
        else if (s == "--name") { a.namePrefix = next(); a.localName = a.namePrefix; }
        else if (s == "--agent") a.agentCap = next();
        else if (s == "--interactive") a.interactive = true;
        else if (s == "--no-pair") a.pair = false;
        else if (s == "--stay") a.stay = true;
        else if (s == "--no-response") a.withResponse = false;
        else if (s == "--max") a.max = std::stoul(next());
        else if (!s.empty() && s[0] == '-') throw std::invalid_argument("unknown option " + s);
        else if (a.command.empty()) a.command = s;
        else a.positional.push_back(s);
    }
    return !a.command.empty();
}

Agent::Capability parseCapability(const std::string& s, Agent::Capability def) {
    if (s.empty()) return def;
    for (auto c : {Agent::Capability::DisplayOnly, Agent::Capability::DisplayYesNo,
                   Agent::Capability::KeyboardOnly, Agent::Capability::NoInputNoOutput,
                   Agent::Capability::KeyboardDisplay})
        if (s == Agent::capabilityString(c)) return c;
    throw std::invalid_argument("unknown agent capability " + s);
}

bool askYesNo(const std::string& question) {
    std::cout << question << " [y/N] " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
}

void configureAgent(Agent& agent, bool interactive) {
    if (!interactive) return;  // defaults: accept everything (headless use)
    agent.onConfirm = [](const std::string& dev, uint32_t pk) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%06u", pk);
        return askYesNo("Confirm passkey " + std::string(buf) + " for " + dev + "?");
    };
    agent.onAuthorize = [](const std::string& dev) { return askYesNo("Allow pairing with " + dev + "?"); };
    agent.onAuthorizeService = [](const std::string& dev, const std::string& uuid) {
        return askYesNo("Allow " + dev + " to use service " + uuid + "?");
    };
    agent.onRequestPasskey = [](const std::string& dev) {
        std::cout << "Enter passkey shown on " << dev << ": " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        return static_cast<uint32_t>(std::stoul(line));
    };
    agent.onRequestPinCode = [](const std::string& dev) {
        std::cout << "Enter PIN for " << dev << ": " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        return line;
    };
}

void printDevice(const char* tag, const DeviceInfo& d) {
    std::printf("%-5s %s  %-8s rssi=%-5s %-24s%s%s%s\n", tag, d.address.c_str(),
                d.addressType.c_str(), d.rssi ? std::to_string(*d.rssi).c_str() : "-",
                d.displayName().c_str(), d.paired ? " [paired]" : "",
                d.trusted ? " [trusted]" : "", d.connected ? " [connected]" : "");
    std::fflush(stdout);
}

void printGattTree(BleManager& mgr, const DeviceInfo& d) {
    std::printf("GATT database of %s (%s):\n", d.address.c_str(), d.displayName().c_str());
    for (const auto& s : mgr.services(d.path)) {
        std::string n = uuidName(s.uuid);
        std::printf("  service %s %s\n", s.uuid.c_str(), n.c_str());
        for (const auto& c : mgr.characteristics(s.path)) {
            std::string flags;
            for (const auto& f : c.flags) flags += (flags.empty() ? "" : ",") + f;
            std::printf("    char %s %-24s [%s]\n", c.uuid.c_str(), uuidName(c.uuid).c_str(),
                        flags.c_str());
        }
    }
    std::fflush(stdout);
}

bool matchesFilter(const Args& a, const DeviceInfo& d) {
    if (!a.namePrefix.empty() && d.displayName().rfind(a.namePrefix, 0) != 0) return false;
    if (a.rssi && (!d.rssi || *d.rssi < *a.rssi)) return false;
    for (const auto& u : a.uuids) {
        bool found = false;
        for (const auto& du : d.uuids) found |= (du == u);
        if (!found) return false;
    }
    return true;
}

void startDiscovery(BleManager& mgr, const Args& a) {
    DiscoveryFilter f;
    f.uuids = a.uuids;
    f.rssi = a.rssi;
    mgr.adapter().setDiscoveryFilter(f);
    mgr.adapter().startDiscovery();
}

// Finds `address` (scanning if BlueZ does not know it yet), then connects.
// `onReady` runs once the device is connected and its services resolved.
class ConnectFlow {
public:
    ConnectFlow(BleManager& mgr, const Args& args, std::string address,
                std::function<void(const DeviceInfo&)> onReady)
        : mgr_(mgr), args_(args), address_(normalizeAddress(address)), onReady_(std::move(onReady)) {}

    void start() {
        if (auto d = mgr_.findByAddress(address_)) {
            if (d->connected && d->servicesResolved) {
                printDevice("READY", *d);
                return onReady_(*d);
            }
            if (d->rssi || d->connected || d->paired) return connect(*d);
        }
        std::printf("Scanning for %s...\n", address_.c_str());
        listener_ = mgr_.subscribeDevices([this](DeviceEvent ev, const DeviceInfo& d) {
            if (ev != DeviceEvent::Removed && d.address == address_ && d.rssi) found(d);
        });
        Args scanArgs;  // no filters: we are looking for one specific address
        startDiscovery(mgr_, scanArgs);
        unsigned t = args_.timeoutSec ? args_.timeoutSec : 20;
        timer_ = addTimeout(t * 1000, [this] {
            timer_ = 0;
            std::fprintf(stderr, "error: %s not found\n", address_.c_str());
            mgr_.quit(2);
            return false;
        });
    }

private:
    void found(const DeviceInfo& d) {
        if (listener_) { mgr_.unsubscribe(listener_); listener_ = 0; }
        if (timer_) { g_source_remove(timer_); timer_ = 0; }
        mgr_.adapter().stopDiscovery();  // connecting while scanning is unreliable
        printDevice("FOUND", d);
        addIdle([this, d] { connect(d); });
    }

    void connect(const DeviceInfo& d) {
        DeviceConnector::Options o;
        o.pair = args_.pair;
        connector_ = DeviceConnector::create(mgr_, d.path, o, [this, path = d.path](bool ok, const std::string& err) {
            if (!ok) {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                mgr_.quit(3);
                return;
            }
            auto info = mgr_.device(path);
            printDevice("READY", *info);
            onReady_(*info);
        });
        connector_->start();
    }

    BleManager& mgr_;
    const Args& args_;
    std::string address_;
    std::function<void(const DeviceInfo&)> onReady_;
    std::shared_ptr<DeviceConnector> connector_;
    int listener_ = 0;
    guint timer_ = 0;
};

// ---------------------------------------------------------------------------

int cmdScan(BleManager& mgr, const Args& a) {
    std::set<std::string> shown;
    mgr.subscribeDevices([&](DeviceEvent ev, const DeviceInfo& d) {
        if (ev == DeviceEvent::Removed) {
            if (shown.erase(d.path)) printDevice("DEL", d);
            return;
        }
        if (!d.rssi || !matchesFilter(a, d)) return;
        if (shown.insert(d.path).second) printDevice("NEW", d);
    });
    startDiscovery(mgr, a);
    unsigned t = a.timeoutSec ? a.timeoutSec : 10;
    std::printf("Scanning on %s for %u s (Ctrl+C to stop)...\n", mgr.adapter().path().c_str(), t);
    addTimeout(t * 1000, [&] { mgr.quit(0); return false; });
    mgr.run();
    mgr.adapter().stopDiscovery();

    std::printf("\n%zu device(s) found:\n", shown.size());
    for (const auto& d : mgr.devices())
        if (shown.count(d.path)) printDevice("", d);
    return mgr.exitCode();
}

int cmdConnect(BleManager& mgr, const Args& a) {
    if (a.positional.size() != 1) throw std::invalid_argument("connect needs MAC");
    ConnectFlow flow(mgr, a, a.positional[0], [&](const DeviceInfo& d) {
        printGattTree(mgr, d);
        if (a.stay) {
            std::printf("Connected; press Ctrl+C to disconnect.\n");
            mgr.subscribeDevices([&mgr, path = d.path](DeviceEvent, const DeviceInfo& x) {
                if (x.path == path && !x.connected) {
                    std::printf("Device disconnected.\n");
                    mgr.quit(4);
                }
            });
        } else {
            mgr.quit(0);
        }
    });
    flow.start();
    mgr.run();
    if (a.stay) {
        if (auto d = mgr.findByAddress(a.positional[0]); d && d->connected) {
            GDBusConnection* c = mgr.connection();
            callSync(c, d->path, iface::kDevice, "Disconnect");
        }
    }
    return mgr.exitCode();
}

int cmdAuto(BleManager& mgr, const Args& a) {
    std::set<std::string> attempted;
    std::vector<std::string> queue;
    std::vector<std::string> connected, failed;
    std::shared_ptr<DeviceConnector> current;
    bool busy = false;
    std::function<void()> pump;

    pump = [&] {
        if (busy) return;
        if (queue.empty()) {
            if (!mgr.adapter().discovering()) startDiscovery(mgr, a);
            return;
        }
        if (a.max && connected.size() >= a.max) { mgr.quit(0); return; }
        busy = true;
        std::string path = queue.front();
        queue.erase(queue.begin());
        mgr.adapter().stopDiscovery();
        DeviceConnector::Options o;
        o.pair = a.pair;
        current = DeviceConnector::create(mgr, path, o, [&, path](bool ok, const std::string& err) {
            auto d = mgr.device(path);
            std::string addr = d ? d->address : path;
            if (ok) {
                connected.push_back(addr);
                printDevice("UP", *d);
                printGattTree(mgr, *d);
            } else {
                failed.push_back(addr);
                std::fprintf(stderr, "FAIL  %s: %s\n", addr.c_str(), err.c_str());
            }
            busy = false;
            addIdle([&] { current.reset(); pump(); });
            if (a.max && connected.size() >= a.max) mgr.quit(0);
        });
        current->start();
    };

    auto consider = [&](const DeviceInfo& d) {
        if (!d.rssi || d.connected) return;
        if (!matchesFilter(a, d) || !attempted.insert(d.path).second) return;
        printDevice("NEW", d);
        queue.push_back(d.path);
        addIdle([&] { pump(); });
    };
    mgr.subscribeDevices([&](DeviceEvent ev, const DeviceInfo& d) {
        if (ev != DeviceEvent::Removed) consider(d);
    });
    // Devices BlueZ already knows and currently sees (RSSI present) produce no
    // "Added" event, so pick them up explicitly.
    for (const auto& d : mgr.devices()) consider(d);

    startDiscovery(mgr, a);
    unsigned t = a.timeoutSec ? a.timeoutSec : 30;
    std::printf("Auto-connecting to discoverable devices for %u s...\n", t);
    addTimeout(t * 1000, [&] { mgr.quit(0); return false; });
    mgr.run();
    mgr.adapter().stopDiscovery();

    std::printf("\nconnected: %zu, failed: %zu\n", connected.size(), failed.size());
    for (const auto& s : connected) std::printf("  OK   %s\n", s.c_str());
    for (const auto& s : failed) std::printf("  FAIL %s\n", s.c_str());
    return mgr.exitCode();
}

int cmdRead(BleManager& mgr, const Args& a) {
    if (a.positional.size() != 2) throw std::invalid_argument("read needs MAC UUID");
    ConnectFlow flow(mgr, a, a.positional[0], [&](const DeviceInfo& d) {
        auto c = mgr.findCharacteristic(d.path, a.positional[1]);
        if (!c) { std::fprintf(stderr, "error: characteristic not found\n"); return mgr.quit(5); }
        // Capture by value: the callback runs after this lambda has returned.
        GattClient(mgr.connection(), c->path).read([&mgr, uuid = c->uuid](bool ok, const std::vector<uint8_t>& v,
                                                                       const std::string& err) {
            if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return mgr.quit(6); }
            std::printf("%s: %s  \"%s\"\n", uuid.c_str(), toHex(v).c_str(), toPrintable(v).c_str());
            mgr.quit(0);
        });
    });
    flow.start();
    mgr.run();
    return mgr.exitCode();
}

int cmdWrite(BleManager& mgr, const Args& a) {
    if (a.positional.size() != 3) throw std::invalid_argument("write needs MAC UUID HEX");
    auto value = parseHex(a.positional[2]);
    ConnectFlow flow(mgr, a, a.positional[0], [&](const DeviceInfo& d) {
        auto c = mgr.findCharacteristic(d.path, a.positional[1]);
        if (!c) { std::fprintf(stderr, "error: characteristic not found\n"); return mgr.quit(5); }
        GattClient(mgr.connection(), c->path).write(value, [&mgr, &value, uuid = c->uuid](bool ok, const std::string& err) {
            if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return mgr.quit(6); }
            std::printf("wrote %zu byte(s) to %s\n", value.size(), uuid.c_str());
            mgr.quit(0);
        }, a.withResponse);
    });
    flow.start();
    mgr.run();
    return mgr.exitCode();
}

int cmdNotify(BleManager& mgr, const Args& a) {
    if (a.positional.size() != 2) throw std::invalid_argument("notify needs MAC UUID");
    std::string charPath;
    ConnectFlow flow(mgr, a, a.positional[0], [&](const DeviceInfo& d) {
        auto c = mgr.findCharacteristic(d.path, a.positional[1]);
        if (!c) { std::fprintf(stderr, "error: characteristic not found\n"); return mgr.quit(5); }
        charPath = c->path;
        mgr.subscribeValues([&](const GattCharacteristicInfo& ci, const std::vector<uint8_t>& v) {
            if (ci.path == charPath)
                std::printf("%s: %s  \"%s\"\n", ci.uuid.c_str(), toHex(v).c_str(), toPrintable(v).c_str()),
                    std::fflush(stdout);
        });
        GattClient(mgr.connection(), charPath).startNotify([&](bool ok, const std::string& err) {
            if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return mgr.quit(6); }
            std::printf("Subscribed; waiting for notifications (Ctrl+C to stop)...\n");
            if (a.timeoutSec) addTimeout(a.timeoutSec * 1000, [&] { mgr.quit(0); return false; });
        });
    });
    flow.start();
    mgr.run();
    if (!charPath.empty()) {
        try {
            callSync(mgr.connection(), charPath, iface::kGattChrc, "StopNotify");
        } catch (const std::exception&) {
        }
    }
    return mgr.exitCode();
}

int cmdDisconnect(BleManager& mgr, const Args& a) {
    if (a.positional.size() != 1) throw std::invalid_argument("disconnect needs MAC");
    auto d = mgr.findByAddress(a.positional[0]);
    if (!d) { std::fprintf(stderr, "error: device not known\n"); return 2; }
    Device(mgr.connection(), d->path).disconnect([&](bool ok, const std::string& err) {
        if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return mgr.quit(3); }
        std::printf("disconnected %s\n", a.positional[0].c_str());
        mgr.quit(0);
    });
    mgr.run();
    return mgr.exitCode();
}

int cmdRemove(BleManager& mgr, const Args& a) {
    if (a.positional.size() != 1) throw std::invalid_argument("remove needs MAC");
    auto d = mgr.findByAddress(a.positional[0]);
    if (!d) { std::fprintf(stderr, "error: device not known\n"); return 2; }
    mgr.adapter().removeDevice(d->path);
    std::printf("removed %s\n", d->address.c_str());
    return 0;
}

int cmdServe(BleManager& mgr, const Args& a) {
    const std::string kEchoService = "12345678-1234-5678-1234-56789abcdef0";
    const std::string kEchoChar    = "12345678-1234-5678-1234-56789abcdef1";
    const std::string kCmdChar     = "12345678-1234-5678-1234-56789abcdef2";

    Adapter& ad = mgr.adapter();
    ad.setAlias(a.localName);
    ad.setPairable(true);
    ad.setDiscoverable(true);

    GattApplication app(mgr.connection());
    LocalService battery{"180F", true, {{"2A19", {"read", "notify"}, {100}, nullptr, nullptr}}};
    LocalService devInfo{"180A", true, {
        {"2A29", {"read"}, {'E', 'x', 'a', 'm', 'p', 'l', 'e'}, nullptr, nullptr},
        {"2A24", {"read"}, {'b', 'l', 'e', '-', 'a', 'p', 'p'}, nullptr, nullptr}}};
    LocalService echo{kEchoService, true, {
        {kEchoChar, {"read", "write", "write-without-response", "notify"}, {}, nullptr,
         [](const std::vector<uint8_t>& v, const std::string& dev) {
             std::printf("echo <- %s: %s \"%s\"\n", dev.c_str(), toHex(v).c_str(), toPrintable(v).c_str());
             std::fflush(stdout);
         }},
        {kCmdChar, {"write"}, {}, nullptr,
         [](const std::vector<uint8_t>& v, const std::string&) {
             if (v.empty() || v[0] > 2) throw std::invalid_argument("unknown command");
             std::printf("command %u received\n", v[0]);
             std::fflush(stdout);
         }}}};
    size_t battIdx = app.addService(battery);
    app.addService(devInfo);
    app.addService(echo);

    Advertisement adv(mgr.connection());
    adv.localName = a.localName;
    adv.serviceUuids = {"180F", kEchoService};
    adv.includeTxPower = true;

    app.registerApplication(ad.path(), [&](bool ok, const std::string& err) {
        if (!ok) { std::fprintf(stderr, "RegisterApplication failed: %s\n", err.c_str()); return mgr.quit(7); }
        std::printf("GATT application registered\n");
        adv.registerAdvertisement(ad.path(), [&](bool ok2, const std::string& err2) {
            if (!ok2) { std::fprintf(stderr, "RegisterAdvertisement failed: %s\n", err2.c_str()); return mgr.quit(8); }
            std::printf("Advertising as \"%s\" on %s (%s). Ctrl+C to stop.\n", a.localName.c_str(),
                        ad.address().c_str(), ad.path().c_str());
        });
    });

    // Simulated battery drain, notified to subscribed centrals.
    uint8_t level = 100;
    addTimeout(5000, [&] {
        level = level > 5 ? level - 1 : 100;
        app.setValue({battIdx, 0}, {level});
        return true;
    });

    mgr.subscribeDevices([](DeviceEvent ev, const DeviceInfo& d) {
        static std::set<std::string> up;
        if (ev == DeviceEvent::Removed) return;
        if (d.connected && up.insert(d.path).second) printDevice("CONN", d);
        if (!d.connected && up.erase(d.path)) printDevice("DISC", d);
    });

    mgr.run();
    try { adv.unregisterAdvertisement(); } catch (const std::exception& e) { g_warning("%s", e.what()); }
    try { app.unregisterApplication(); } catch (const std::exception& e) { g_warning("%s", e.what()); }
    try { ad.setDiscoverable(false); } catch (const std::exception&) {}
    return mgr.exitCode();
}

gboolean onSignal(gpointer data) {
    static_cast<BleManager*>(data)->quit(130);
    return G_SOURCE_CONTINUE;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    try {
        if (!parseArgs(argc, argv, args)) { usage(); return 1; }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        usage();
        return 1;
    }

    try {
        BleManager mgr(args.adapter);
        g_unix_signal_add(SIGINT, onSignal, &mgr);
        g_unix_signal_add(SIGTERM, onSignal, &mgr);

        if (!mgr.adapter().powered()) {
            std::printf("Powering on %s\n", mgr.adapter().path().c_str());
            mgr.adapter().setPowered(true);
        }

        // Register our pairing agent so Pair() / incoming pairings are handled here.
        bool serve = args.command == "serve";
        Agent agent(mgr.connection(),
                    parseCapability(args.agentCap, serve ? Agent::Capability::NoInputNoOutput
                                                         : Agent::Capability::KeyboardDisplay));
        configureAgent(agent, args.interactive);
        agent.registerAgent(true);

        const std::string& c = args.command;
        if (c == "scan") return cmdScan(mgr, args);
        if (c == "connect") return cmdConnect(mgr, args);
        if (c == "auto") return cmdAuto(mgr, args);
        if (c == "read") return cmdRead(mgr, args);
        if (c == "write") return cmdWrite(mgr, args);
        if (c == "notify") return cmdNotify(mgr, args);
        if (c == "disconnect") return cmdDisconnect(mgr, args);
        if (c == "remove") return cmdRemove(mgr, args);
        if (c == "serve") return cmdServe(mgr, args);
        usage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
