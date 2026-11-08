// SPDX-License-Identifier: MIT
// Pure parsers for the wpa_supplicant control interface text protocol.
// Kept free of socket code so they can be unit-tested on the host.
#pragma once

#include <cstddef>

#include "wifi6/types.hpp"
#include "wifi6/wifi_driver.hpp"

namespace wifi6 {

// Classify the flags column of SCAN_RESULTS, e.g. "[WPA2-PSK-CCMP][SAE-CCMP][ESS]".
Security securityFromFlags(const char* flags) noexcept;

// Parse one SCAN_RESULTS line: bssid \t freq \t signal \t flags \t ssid
bool parseScanResultLine(const char* line, ApInfo& out) noexcept;

// Parse a SIGNAL_POLL reply (RSSI=, LINKSPEED=, NOISE=, FREQUENCY=).
bool parseSignalPoll(const char* text, LinkStats& out) noexcept;

// Parse an unsolicited monitor message such as
// "<3>CTRL-EVENT-CONNECTED - Connection to 02:00:00:00:01:00 completed ...".
bool parseEvent(const char* msg, DriverEvent& out) noexcept;

// Find `key=value` where key starts a line or follows whitespace.
bool findValue(const char* text, const char* key, char* out, size_t cap) noexcept;

}  // namespace wifi6
