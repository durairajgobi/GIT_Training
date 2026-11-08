// SPDX-License-Identifier: MIT
// Core Wi-Fi data types: bands, security modes, BSS descriptions and link
// statistics. Fixed-size storage only - no heap allocation.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "wifi6/status.hpp"

namespace wifi6 {

enum class Band : uint8_t { Unknown, Ghz2_4, Ghz5, Ghz6 };

// Map a channel centre frequency (MHz) to its band / IEEE channel number.
// 6 GHz (Wi-Fi 6E) uses the 5950 MHz channel starting frequency.
Band bandFromFreq(uint32_t freqMhz) noexcept;
uint16_t channelFromFreq(uint32_t freqMhz) noexcept;
const char* toString(Band b) noexcept;

enum class Security : uint8_t {
    Open,
    Owe,          // Enhanced Open (opportunistic wireless encryption)
    Wpa2Psk,
    Wpa3Sae,
    Wpa2Wpa3,     // WPA2/WPA3 transition mode (PSK + SAE)
    Enterprise,   // 802.1X - not handled by this application
    Unsupported,  // WEP / unknown
};
const char* toString(Security s) noexcept;

struct Bssid {
    std::array<uint8_t, 6> octets{};

    // Strict "aa:bb:cc:dd:ee:ff" parser.
    static bool parse(const char* text, Bssid& out) noexcept;
    // Writes "aa:bb:cc:dd:ee:ff"; cap must be >= 18.
    void format(char* out, size_t cap) const noexcept;
    bool isZero() const noexcept;

    friend bool operator==(const Bssid& a, const Bssid& b) noexcept { return a.octets == b.octets; }
    friend bool operator!=(const Bssid& a, const Bssid& b) noexcept { return !(a == b); }
};

constexpr size_t kMaxSsidLen = 32;
constexpr size_t kMaxPassphraseLen = 63;
constexpr size_t kMaxScanResults = 32;

// One BSS from a scan, enriched with 802.11ax (HE) capabilities.
struct ApInfo {
    char ssid[kMaxSsidLen + 1]{};
    Bssid bssid{};
    uint32_t freqMhz = 0;
    int16_t rssiDbm = -127;
    Security security = Security::Open;
    bool heCapable = false;     // HE Capabilities element present (Wi-Fi 6)
    bool twtResponder = false;  // AP can act as Target Wake Time responder
    uint8_t bssColor = 0;       // HE BSS colour (0 = unknown / disabled)

    Band band() const noexcept { return bandFromFreq(freqMhz); }
};

// Credentials and preferences for the network we want to join.
struct NetworkConfig {
    char ssid[kMaxSsidLen + 1]{};
    char passphrase[kMaxPassphraseLen + 1]{};
    Security security = Security::Wpa3Sae;
    int16_t minRssiDbm = -82;
    bool allow2g4 = true;
    bool allow5g = true;
    bool allow6g = true;

    Status setSsid(const char* s) noexcept;
    Status setPassphrase(const char* p) noexcept;
    Status validate() const noexcept;
    bool bandAllowed(Band b) const noexcept;
    bool needsPassphrase() const noexcept;
};

constexpr int16_t kNoiseUnknown = 0;

struct LinkStats {
    int16_t rssiDbm = -127;
    int16_t noiseDbm = kNoiseUnknown;
    uint32_t txRateKbps = 0;
    uint32_t freqMhz = 0;
    uint8_t heMcs = 0xFF;  // 0xFF = unknown
    uint8_t nss = 0;
};

// Overwrite memory holding secrets in a way the optimiser cannot elide.
inline void secureZero(void* p, size_t n) noexcept {
    volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
    while (n--) *v++ = 0;
}

}  // namespace wifi6
