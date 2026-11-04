// SPDX-License-Identifier: MIT
#include "shell.hpp"

#include <cstdio>
#include <sstream>
#include <vector>

namespace blemgrd {

using namespace blemgr;

namespace {

void out(const char* fmt, const std::string& a = {}, const std::string& b = {}) {
    std::printf(fmt, a.c_str(), b.c_str());
    std::fflush(stdout);
}

StatusCallback report(const std::string& what) {
    return [what](const Status& s) { out("%s: %s\n", what, s.toString()); };
}

std::string describe(const DeviceSnapshot& d) {
    std::ostringstream os;
    os << d.info.address << "  " << toString(d.state);
    if (d.managed) os << " (managed, retries " << d.reconnectAttempts << ")";
    if (d.info.rssi) os << "  rssi " << *d.info.rssi;
    if (!d.info.name.empty()) os << "  \"" << d.info.name << "\"";
    if (d.info.paired) os << "  paired";
    return os.str();
}

}  // namespace

void printHelp() {
    std::puts(
        "commands:\n"
        "  status                              adapter / scan state\n"
        "  power on|off\n"
        "  scan on [uuid ...] | scan off\n"
        "  devices                             list known devices\n"
        "  info <addr>                         services and subscriptions\n"
        "  connect <addr> [pair] [once]        once = no auto-reconnect\n"
        "  disconnect <addr>\n"
        "  forget <addr>                       disconnect and remove bond\n"
        "  read <addr> <svc> <chr>\n"
        "  write <addr> <svc> <chr> <hex> [noresp]\n"
        "  sub|unsub <addr> <svc> <chr>\n"
        "  adv on [name] | adv off\n"
        "  quit");
}

bool runCommand(BleManager& mgr, const std::string& line) {
    std::istringstream is(line);
    std::vector<std::string> a;
    for (std::string w; is >> w;) a.push_back(w);
    if (a.empty()) return true;
    const std::string& cmd = a[0];
    auto need = [&](size_t n) {
        if (a.size() >= n) return true;
        out("%s: missing arguments (try 'help')\n", cmd);
        return false;
    };

    if (cmd == "quit" || cmd == "exit") return false;
    if (cmd == "help" || cmd == "?") {
        printHelp();
    } else if (cmd == "status") {
        out("adapter %s, scanning %s\n", toString(mgr.adapterState()), mgr.scanning() ? "yes" : "no");
    } else if (cmd == "power" && need(2)) {
        mgr.setPowered(a[1] == "on", report("power"));
    } else if (cmd == "scan" && need(2)) {
        if (a[1] == "on") {
            ScanFilter f;
            f.serviceUuids.assign(a.begin() + 2, a.end());
            mgr.startScan(f, {}, report("scan on"));
        } else {
            mgr.stopScan(report("scan off"));
        }
    } else if (cmd == "devices") {
        auto devs = mgr.devices();
        if (devs.empty()) out("no devices\n");
        for (const auto& d : devs) out("%s\n", describe(d));
    } else if (cmd == "info" && need(2)) {
        auto d = mgr.device(a[1]);
        if (!d) {
            out("%s: unknown\n", a[1]);
            return true;
        }
        out("%s\n", describe(*d));
        for (const auto& s : d->services) {
            out("  service %s\n", s.uuid);
            for (const auto& c : s.characteristics) {
                std::string flags;
                for (const auto& f : c.flags) flags += f + " ";
                out("    char %s  [%s]\n", c.uuid, flags);
            }
        }
        for (const auto& s : d->subscriptions) out("  subscribed %s\n", s.toString());
    } else if (cmd == "connect" && need(2)) {
        ConnectOptions o;
        for (size_t i = 2; i < a.size(); ++i) {
            if (a[i] == "pair") o.pair = true;
            if (a[i] == "once") o.autoReconnect = false;
        }
        mgr.connect(a[1], o, report("connect " + a[1]));
    } else if (cmd == "disconnect" && need(2)) {
        mgr.disconnect(a[1], report("disconnect " + a[1]));
    } else if (cmd == "forget" && need(2)) {
        mgr.forget(a[1], report("forget " + a[1]));
    } else if (cmd == "read" && need(4)) {
        std::string what = a[3];
        mgr.read(a[1], {a[2], a[3]}, [what](const Status& s, const Bytes& v) {
            if (s.ok())
                out("read %s: %s\n", what, toHex(v));
            else
                out("read %s: %s\n", what, s.toString());
        });
    } else if (cmd == "write" && need(5)) {
        auto v = parseHex(a[4]);
        if (!v) {
            out("bad hex: %s\n", a[4]);
            return true;
        }
        WriteType t = (a.size() > 5 && a[5] == "noresp") ? WriteType::WithoutResponse
                                                         : WriteType::WithResponse;
        mgr.write(a[1], {a[2], a[3]}, *v, t, report("write " + a[3]));
    } else if ((cmd == "sub" || cmd == "unsub") && need(4)) {
        if (cmd == "sub")
            mgr.subscribe(a[1], {a[2], a[3]}, report("subscribe " + a[3]));
        else
            mgr.unsubscribe(a[1], {a[2], a[3]}, report("unsubscribe " + a[3]));
    } else if (cmd == "adv" && need(2)) {
        if (a[1] == "on") {
            AdvertisementData adv;
            adv.localName = a.size() > 2 ? a[2] : "TCU-BLE";
            adv.includeTxPower = true;
            mgr.startAdvertising(adv, report("advertising"));
        } else {
            mgr.stopAdvertising(report("advertising off"));
        }
    } else if (a.size() > 0 && cmd != "power" && cmd != "scan" && cmd != "info" &&
               cmd != "connect" && cmd != "disconnect" && cmd != "forget" && cmd != "read" &&
               cmd != "write" && cmd != "sub" && cmd != "unsub" && cmd != "adv") {
        out("unknown command '%s' (try 'help')\n", cmd);
    }
    return true;
}

}  // namespace blemgrd
