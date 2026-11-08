// SPDX-License-Identifier: MIT
#include "wifi6/types.hpp"

#include <cstdio>
#include <cstring>

namespace wifi6 {

Band bandFromFreq(uint32_t f) noexcept {
    if (f >= 2400 && f <= 2500) return Band::Ghz2_4;
    if (f >= 4900 && f < 5925) return Band::Ghz5;
    if (f >= 5925 && f <= 7125) return Band::Ghz6;
    return Band::Unknown;
}

uint16_t channelFromFreq(uint32_t f) noexcept {
    switch (bandFromFreq(f)) {
        case Band::Ghz2_4:
            if (f == 2484) return 14;
            if (f >= 2412 && f <= 2472) return static_cast<uint16_t>((f - 2407) / 5);
            return 0;
        case Band::Ghz5:
            if (f < 5000) return static_cast<uint16_t>((f - 4000) / 5);  // 4.9 GHz public safety
            return static_cast<uint16_t>((f - 5000) / 5);
        case Band::Ghz6:
            if (f == 5935) return 2;  // the odd one out in 6 GHz
            if (f < 5955) return 0;
            return static_cast<uint16_t>((f - 5950) / 5);
        case Band::Unknown: break;
    }
    return 0;
}

const char* toString(Band b) noexcept {
    switch (b) {
        case Band::Ghz2_4: return "2.4GHz";
        case Band::Ghz5: return "5GHz";
        case Band::Ghz6: return "6GHz";
        case Band::Unknown: break;
    }
    return "unknown";
}

const char* toString(Security s) noexcept {
    switch (s) {
        case Security::Open: return "open";
        case Security::Owe: return "owe";
        case Security::Wpa2Psk: return "wpa2-psk";
        case Security::Wpa3Sae: return "wpa3-sae";
        case Security::Wpa2Wpa3: return "wpa2/wpa3";
        case Security::Enterprise: return "enterprise";
        case Security::Unsupported: break;
    }
    return "unsupported";
}

namespace {
int hexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool printableAscii(const char* s) noexcept {
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}
}  // namespace

bool Bssid::parse(const char* text, Bssid& out) noexcept {
    if (!text || std::strlen(text) < 17) return false;
    Bssid b;
    for (size_t i = 0; i < 6; ++i) {
        const char* p = text + i * 3;
        const int hi = hexNibble(p[0]);
        const int lo = hexNibble(p[1]);
        if (hi < 0 || lo < 0) return false;
        if (i < 5 && p[2] != ':') return false;
        b.octets[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    const char tail = text[17];
    if (tail != '\0' && tail != ' ' && tail != '\t' && tail != '\n') return false;
    out = b;
    return true;
}

void Bssid::format(char* out, size_t cap) const noexcept {
    std::snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x", octets[0], octets[1], octets[2], octets[3], octets[4],
                  octets[5]);
}

bool Bssid::isZero() const noexcept {
    for (uint8_t o : octets)
        if (o) return false;
    return true;
}

Status NetworkConfig::setSsid(const char* s) noexcept {
    if (!s) return Status::InvalidArg;
    const size_t n = std::strlen(s);
    if (n == 0 || n > kMaxSsidLen) return Status::InvalidArg;
    std::memcpy(ssid, s, n + 1);
    return Status::Ok;
}

Status NetworkConfig::setPassphrase(const char* p) noexcept {
    if (!p) return Status::InvalidArg;
    const size_t n = std::strlen(p);
    if (n > kMaxPassphraseLen) return Status::InvalidArg;
    secureZero(passphrase, sizeof passphrase);
    std::memcpy(passphrase, p, n + 1);
    return Status::Ok;
}

bool NetworkConfig::needsPassphrase() const noexcept {
    return security == Security::Wpa2Psk || security == Security::Wpa3Sae || security == Security::Wpa2Wpa3;
}

bool NetworkConfig::bandAllowed(Band b) const noexcept {
    switch (b) {
        case Band::Ghz2_4: return allow2g4;
        case Band::Ghz5: return allow5g;
        case Band::Ghz6: return allow6g;
        case Band::Unknown: break;
    }
    return false;
}

Status NetworkConfig::validate() const noexcept {
    const size_t ssidLen = strnlen(ssid, sizeof ssid);
    if (ssidLen == 0 || ssidLen > kMaxSsidLen) return Status::InvalidArg;
    if (!allow2g4 && !allow5g && !allow6g) return Status::InvalidArg;

    const size_t passLen = strnlen(passphrase, sizeof passphrase);
    switch (security) {
        case Security::Open:
        case Security::Owe:
            return Status::Ok;
        case Security::Wpa2Psk:
        case Security::Wpa2Wpa3:
            // WPA2 passphrase: 8..63 printable ASCII characters (IEEE 802.11 Annex J).
            if (passLen < 8 || passLen > kMaxPassphraseLen) return Status::InvalidArg;
            return printableAscii(passphrase) ? Status::Ok : Status::InvalidArg;
        case Security::Wpa3Sae:
            // SAE has no minimum length, but an empty password is never valid.
            if (passLen == 0) return Status::InvalidArg;
            return printableAscii(passphrase) ? Status::Ok : Status::InvalidArg;
        case Security::Enterprise:
        case Security::Unsupported:
            break;
    }
    return Status::NotSupported;
}

}  // namespace wifi6
