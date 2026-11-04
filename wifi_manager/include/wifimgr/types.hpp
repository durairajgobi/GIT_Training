// Domain types shared across the Wi-Fi manager.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace wifimgr {

enum class Security {
  Open,       // no encryption
  WpaPsk,     // WPA/WPA2 personal
  Sae,        // WPA3 personal
  WpaPskSae,  // WPA2/WPA3 transition mode
  WpaEap,     // WPA2/WPA3 enterprise (802.1X)
  Wep,        // legacy, recognised in scans but never connected to
  Unknown,    // e.g. OWE or anything else we do not support
};

const char* toString(Security s);
std::optional<Security> securityFromString(std::string_view s);

// Can a profile configured for `profile` security safely be used on a BSS
// advertising `bss`? Prevents downgrades, e.g. a PSK profile joining an open
// AP that spoofs the same SSID.
bool securityCompatible(Security profile, Security bss);

struct ScanResult {
  std::string bssid;
  int frequency = 0;  // MHz
  int rssi = -100;    // dBm
  std::string flags;  // raw wpa_supplicant flags, e.g. [WPA2-PSK-CCMP][ESS]
  std::string ssid;   // decoded bytes; empty for hidden BSSs
  Security security = Security::Unknown;
};

// A saved network. Secrets are never logged or returned over the control
// socket.
struct Profile {
  std::string ssid;
  Security security = Security::WpaPsk;
  std::string psk;  // WPA-PSK: 8..63 char passphrase or 64 hex digits; SAE: password
  // 802.1X (Security::WpaEap)
  std::string eap = "PEAP";
  std::string identity;
  std::string anonymousIdentity;
  std::string password;
  std::string phase2 = "auth=MSCHAPV2";
  std::string caCert;
  int priority = 0;
  bool hidden = false;
  bool autoconnect = true;

  // Returns an error description, or nullopt if the profile is usable.
  std::optional<std::string> validate() const;
};

enum class ConnState {
  Unavailable,  // wpa_supplicant not reachable
  Idle,         // reachable, not connected, waiting (backoff or user disconnect)
  Scanning,     // scan requested, waiting for results
  Connecting,   // network selected, waiting for association/authentication
  ObtainingIp,  // associated, waiting for a DHCP lease
  Connected,    // associated with an IPv4 address
};

const char* toString(ConnState s);

struct LinkInfo {
  std::string wpaState;
  std::string bssid;
  std::string ssid;
  int frequency = 0;
  int rssi = -100;
  int linkSpeedMbps = 0;
  std::string ipAddress;
};

}  // namespace wifimgr
