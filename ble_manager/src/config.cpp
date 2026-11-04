// SPDX-License-Identifier: MIT
#include "blemgr/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace blemgr {

namespace {

std::string trim(std::string s) {
    auto ns = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parseBool(const std::string& v, bool* out) {
    std::string l = lower(v);
    if (l == "1" || l == "true" || l == "yes" || l == "on") return *out = true, true;
    if (l == "0" || l == "false" || l == "no" || l == "off") return *out = false, true;
    return false;
}

template <class T>
bool parseNum(const std::string& v, T* out) {
    std::istringstream is(v);
    T t{};
    if (!(is >> t)) return false;
    std::string rest;
    if (is >> rest) return false;
    *out = t;
    return true;
}

bool parseMs(const std::string& v, std::chrono::milliseconds* out) {
    long long n = 0;
    if (!parseNum(v, &n) || n < 0) return false;
    *out = std::chrono::milliseconds(n);
    return true;
}

}  // namespace

std::optional<DaemonConfig> parseConfig(std::istream& in, std::string* error) {
    DaemonConfig cfg;
    std::string section;
    ManagedDevice* dev = nullptr;
    std::string line;
    int lineNo = 0;

    auto fail = [&](const std::string& why) -> std::optional<DaemonConfig> {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + why;
        return std::nullopt;
    };

    while (std::getline(in, line)) {
        ++lineNo;
        auto hash = line.find_first_of("#;");
        if (hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') return fail("unterminated section");
            section = trim(line.substr(1, line.size() - 2));
            dev = nullptr;
            if (lower(section.substr(0, 7)) == "device ") {
                auto a = normalizeAddress(trim(section.substr(7)));
                if (!a) return fail("bad device address in [" + section + "]");
                for (auto& d : cfg.devices)
                    if (d.address == *a) return fail("duplicate device " + *a);
                cfg.devices.push_back({*a, {}});
                dev = &cfg.devices.back();
                section = "device";
            } else {
                section = lower(section);
                static const char* known[] = {"general", "adapter", "agent", "connection",
                                              "reconnect", "peripheral"};
                if (std::none_of(std::begin(known), std::end(known),
                                 [&](const char* k) { return section == k; }))
                    return fail("unknown section [" + section + "]");
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) return fail("expected key = value");
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        bool ok = true;
        auto& m = cfg.manager;
        auto& b = cfg.bluez;

        if (section == "general") {
            if (key == "log_level") {
                std::string l = lower(val);
                if (l == "error") cfg.logLevel = LogLevel::Error;
                else if (l == "warn" || l == "warning") cfg.logLevel = LogLevel::Warn;
                else if (l == "info") cfg.logLevel = LogLevel::Info;
                else if (l == "debug") cfg.logLevel = LogLevel::Debug;
                else ok = false;
            } else {
                return fail("unknown key " + key);
            }
        } else if (section == "adapter") {
            if (key == "name") b.adapter = val;
            else if (key == "power_on") ok = parseBool(val, &m.powerOnStart);
            else if (key == "bus_address") b.busAddress = val;
            else return fail("unknown key " + key);
        } else if (section == "agent") {
            if (key == "capability") {
                static const char* caps[] = {"", "NoInputNoOutput", "DisplayOnly", "DisplayYesNo",
                                             "KeyboardOnly", "KeyboardDisplay"};
                ok = std::any_of(std::begin(caps), std::end(caps),
                                 [&](const char* c) { return val == c; });
                if (ok) b.agentCapability = val;
            } else if (key == "auto_accept") ok = parseBool(val, &b.agentAutoAccept);
            else if (key == "passkey") ok = parseNum(val, &b.agentPasskey) && b.agentPasskey <= 999999;
            else if (key == "pin_code") b.agentPinCode = val;
            else return fail("unknown key " + key);
        } else if (section == "connection") {
            if (key == "timeout_ms") ok = parseMs(val, &m.connectTimeout) && m.connectTimeout.count() > 0;
            else if (key == "max_connections") ok = parseNum(val, &m.maxConnections) && m.maxConnections > 0;
            else if (key == "pause_scan_during_connect") ok = parseBool(val, &m.pauseScanDuringConnect);
            else if (key == "stale_device_timeout_ms") ok = parseMs(val, &m.staleDeviceTimeout);
            else return fail("unknown key " + key);
        } else if (section == "reconnect") {
            auto& r = m.reconnect;
            if (key == "initial_delay_ms") ok = parseMs(val, &r.initialDelay);
            else if (key == "max_delay_ms") ok = parseMs(val, &r.maxDelay);
            else if (key == "multiplier") ok = parseNum(val, &r.multiplier) && r.multiplier >= 1.0;
            else if (key == "jitter") ok = parseNum(val, &r.jitter) && r.jitter >= 0 && r.jitter <= 1;
            else if (key == "max_attempts") ok = parseNum(val, &r.maxAttempts);
            else return fail("unknown key " + key);
        } else if (section == "peripheral") {
            auto& p = cfg.peripheral;
            if (key == "enabled") ok = parseBool(val, &p.enabled);
            else if (key == "name") ok = !(p.name = val).empty();
            else if (key == "notify_interval_ms") ok = parseNum(val, &p.notifyIntervalMs) && p.notifyIntervalMs >= 100;
            else return fail("unknown key " + key);
        } else if (section == "device" && dev) {
            if (key == "auto_reconnect") ok = parseBool(val, &dev->options.autoReconnect);
            else if (key == "pair") ok = parseBool(val, &dev->options.pair);
            else if (key == "subscribe") {
                auto slash = val.find('/');
                if (slash == std::string::npos) return fail("subscribe expects <service>/<char>");
                auto ref = CharRef{trim(val.substr(0, slash)), trim(val.substr(slash + 1))}.normalized();
                if (!ref) return fail("bad uuid in " + val);
                dev->options.subscriptions.push_back(*ref);
            } else {
                return fail("unknown key " + key);
            }
        } else {
            return fail("key outside of a section");
        }
        if (!ok) return fail("invalid value for " + key + ": " + val);
    }
    if (cfg.manager.reconnect.maxDelay < cfg.manager.reconnect.initialDelay) {
        lineNo = 0;
        return fail("reconnect.max_delay_ms < initial_delay_ms");
    }
    return cfg;
}

std::optional<DaemonConfig> loadConfigFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return std::nullopt;
    }
    std::string err;
    auto cfg = parseConfig(f, &err);
    if (!cfg && error) *error = path + ": " + err;
    return cfg;
}

}  // namespace blemgr
