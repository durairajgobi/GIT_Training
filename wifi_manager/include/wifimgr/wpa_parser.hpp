// Parsers for wpa_supplicant control interface replies and events.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "wifimgr/types.hpp"

namespace wifimgr {

Security securityFromFlags(std::string_view flags);

// SCAN_RESULTS: header line then "bssid\tfreq\tsignal\tflags\tssid".
std::vector<ScanResult> parseScanResults(std::string_view reply);

// STATUS / SIGNAL_POLL: "key=value" lines.
std::map<std::string, std::string> parseKeyValues(std::string_view reply);

struct SupplicantEvent {
  enum class Type {
    ScanResults,
    ScanFailed,
    Connected,
    Disconnected,
    AuthFailure,  // wrong key / EAP failure
    ConnFailed,   // association or authentication rejected by the AP
    NetworkNotFound,
    Terminating,
    Other,
  };
  Type type = Type::Other;
  std::string bssid;
  std::string ssid;
  std::string reason;
  bool locallyGenerated = false;
  std::string raw;  // without the "<N>" priority prefix
};

SupplicantEvent parseEvent(std::string_view msg);

// Value of `key=` in an event line. Handles key="quoted \" value".
std::string eventField(std::string_view msg, std::string_view key);

}  // namespace wifimgr
