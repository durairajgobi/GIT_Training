// SPDX-License-Identifier: MIT
#include "wifi6/wpa_parse.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wifi6 {

namespace {

bool startsWith(const char* s, const char* prefix) noexcept { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

bool toLong(const char* s, long& out) noexcept {
    if (!s || !*s) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (errno != 0 || end == s) return false;
    while (*end == ' ' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return false;
    out = v;
    return true;
}

int16_t clampRssi(long v) noexcept {
    if (v < -127) return -127;
    if (v > 20) return 20;
    return static_cast<int16_t>(v);
}

const char* skipLevelPrefix(const char* m) noexcept {
    if (m[0] == '<') {
        const char* e = std::strchr(m, '>');
        if (e) return e + 1;
    }
    return m;
}

}  // namespace

bool findValue(const char* text, const char* key, char* out, size_t cap) noexcept {
    if (!text || !key || !out || cap == 0) return false;
    const size_t klen = std::strlen(key);
    for (const char* p = text; (p = std::strstr(p, key)) != nullptr; p += klen) {
        const bool boundary = p == text || p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t';
        if (!boundary || p[klen] != '=') continue;
        const char* v = p + klen + 1;
        size_t n = 0;
        while (v[n] && v[n] != ' ' && v[n] != '\n' && v[n] != '\t' && v[n] != '\r') ++n;
        if (n + 1 > cap) return false;
        std::memcpy(out, v, n);
        out[n] = '\0';
        return true;
    }
    return false;
}

Security securityFromFlags(const char* flags) noexcept {
    if (!flags) return Security::Open;
    const bool sae = std::strstr(flags, "SAE") != nullptr;
    const bool psk = std::strstr(flags, "PSK") != nullptr;
    const bool owe = std::strstr(flags, "OWE") != nullptr;
    const bool eap = std::strstr(flags, "EAP") != nullptr;
    const bool wep = std::strstr(flags, "WEP") != nullptr;
    if (sae && psk) return Security::Wpa2Wpa3;
    if (sae) return Security::Wpa3Sae;
    if (owe) return Security::Owe;
    if (psk) return Security::Wpa2Psk;
    if (eap) return Security::Enterprise;
    if (wep) return Security::Unsupported;
    return Security::Open;
}

bool parseScanResultLine(const char* line, ApInfo& out) noexcept {
    if (!line) return false;
    char buf[512];
    const size_t n = std::strlen(line);
    if (n >= sizeof buf) return false;
    std::memcpy(buf, line, n + 1);

    char* fields[5] = {};
    size_t nf = 0;
    fields[nf++] = buf;
    for (char* p = buf; *p && nf < 5; ++p) {
        if (*p == '\t') {
            *p = '\0';
            fields[nf++] = p + 1;
        }
    }
    if (nf < 4) return false;

    ApInfo ap;
    if (!Bssid::parse(fields[0], ap.bssid)) return false;
    long freq = 0, sig = 0;
    if (!toLong(fields[1], freq) || freq <= 0 || freq > 100000) return false;
    if (!toLong(fields[2], sig)) return false;
    ap.freqMhz = static_cast<uint32_t>(freq);
    ap.rssiDbm = clampRssi(sig);
    ap.security = securityFromFlags(fields[3]);

    if (nf == 5) {
        char* ssid = fields[4];
        size_t len = std::strlen(ssid);
        while (len && (ssid[len - 1] == '\r' || ssid[len - 1] == '\n')) ssid[--len] = '\0';
        std::snprintf(ap.ssid, sizeof ap.ssid, "%s", ssid);
    }
    out = ap;
    return true;
}

bool parseSignalPoll(const char* text, LinkStats& out) noexcept {
    char v[32];
    long n = 0;
    LinkStats s;
    if (!findValue(text, "RSSI", v, sizeof v) || !toLong(v, n)) return false;
    s.rssiDbm = clampRssi(n);
    if (findValue(text, "LINKSPEED", v, sizeof v) && toLong(v, n) && n >= 0)
        s.txRateKbps = static_cast<uint32_t>(n) * 1000u;  // reported in Mbit/s
    if (findValue(text, "NOISE", v, sizeof v) && toLong(v, n) && n != 9999 && n >= -127 && n <= 0)
        s.noiseDbm = static_cast<int16_t>(n);
    if (findValue(text, "FREQUENCY", v, sizeof v) && toLong(v, n) && n > 0)
        s.freqMhz = static_cast<uint32_t>(n);
    out = s;
    return true;
}

bool parseEvent(const char* msg, DriverEvent& out) noexcept {
    if (!msg) return false;
    const char* m = skipLevelPrefix(msg);
    DriverEvent ev;
    char v[64];
    long n = 0;

    if (startsWith(m, "CTRL-EVENT-SCAN-RESULTS")) {
        ev.type = DriverEventType::ScanDone;
    } else if (startsWith(m, "CTRL-EVENT-SCAN-FAILED")) {
        ev.type = DriverEventType::ScanFailed;
    } else if (startsWith(m, "CTRL-EVENT-CONNECTED")) {
        ev.type = DriverEventType::Connected;
        const char* at = std::strstr(m, "Connection to ");
        if (at) Bssid::parse(at + 14, ev.bssid);
    } else if (startsWith(m, "CTRL-EVENT-DISCONNECTED")) {
        ev.type = DriverEventType::Disconnected;
        if (findValue(m, "bssid", v, sizeof v)) Bssid::parse(v, ev.bssid);
        if (findValue(m, "reason", v, sizeof v) && toLong(v, n) && n >= 0 && n <= 0xFFFF)
            ev.reason = static_cast<uint16_t>(n);
    } else if (startsWith(m, "CTRL-EVENT-SSID-TEMP-DISABLED")) {
        const bool cred = findValue(m, "reason", v, sizeof v) &&
                          (std::strcmp(v, "WRONG_KEY") == 0 || std::strcmp(v, "AUTH_FAILED") == 0);
        ev.type = cred ? DriverEventType::AuthFailed : DriverEventType::ConnectFailed;
    } else if (startsWith(m, "CTRL-EVENT-AUTH-REJECT") || startsWith(m, "CTRL-EVENT-ASSOC-REJECT")) {
        ev.type = DriverEventType::ConnectFailed;
        if (findValue(m, "bssid", v, sizeof v)) Bssid::parse(v, ev.bssid);
        if (findValue(m, "status_code", v, sizeof v) && toLong(v, n) && n >= 0 && n <= 0xFFFF)
            ev.reason = static_cast<uint16_t>(n);
    } else if (startsWith(m, "CTRL-EVENT-NETWORK-NOT-FOUND")) {
        ev.type = DriverEventType::NetworkNotFound;
    } else if (startsWith(m, "CTRL-EVENT-SIGNAL-CHANGE")) {
        ev.type = DriverEventType::SignalChange;
        if (findValue(m, "signal", v, sizeof v) && toLong(v, n)) ev.rssiDbm = clampRssi(n);
    } else if (startsWith(m, "CTRL-EVENT-TERMINATING")) {
        ev.type = DriverEventType::DriverTerminated;
    } else {
        return false;
    }
    out = ev;
    return true;
}

}  // namespace wifi6
