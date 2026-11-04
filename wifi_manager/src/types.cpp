#include "wifimgr/types.hpp"

#include "wifimgr/strutil.hpp"

namespace wifimgr {

const char* toString(Security s) {
  switch (s) {
    case Security::Open: return "open";
    case Security::WpaPsk: return "wpa-psk";
    case Security::Sae: return "sae";
    case Security::WpaPskSae: return "wpa-psk-sae";
    case Security::WpaEap: return "wpa-eap";
    case Security::Wep: return "wep";
    case Security::Unknown: return "unknown";
  }
  return "unknown";
}

std::optional<Security> securityFromString(std::string_view s) {
  if (s == "open" || s == "none") return Security::Open;
  if (s == "wpa-psk" || s == "wpa2" || s == "psk") return Security::WpaPsk;
  if (s == "sae" || s == "wpa3") return Security::Sae;
  if (s == "wpa-psk-sae" || s == "wpa2-wpa3") return Security::WpaPskSae;
  if (s == "wpa-eap" || s == "eap" || s == "enterprise") return Security::WpaEap;
  return std::nullopt;
}

bool securityCompatible(Security profile, Security bss) {
  switch (profile) {
    case Security::Open: return bss == Security::Open;
    case Security::WpaPsk: return bss == Security::WpaPsk || bss == Security::WpaPskSae;
    case Security::Sae: return bss == Security::Sae || bss == Security::WpaPskSae;
    case Security::WpaPskSae:
      return bss == Security::WpaPsk || bss == Security::Sae || bss == Security::WpaPskSae;
    case Security::WpaEap: return bss == Security::WpaEap;
    case Security::Wep:
    case Security::Unknown: return false;
  }
  return false;
}

std::optional<std::string> Profile::validate() const {
  if (ssid.empty() || ssid.size() > 32) return "SSID must be 1..32 bytes";
  auto passphraseOk = [](const std::string& p) {
    if (p.size() == 64) return isHexString(p);
    if (p.size() < 8 || p.size() > 63) return false;
    for (unsigned char c : p)
      if (c < 0x20 || c > 0x7e) return false;  // WPA passphrases are printable ASCII
    return true;
  };
  switch (security) {
    case Security::Open:
      if (!psk.empty()) return "open network must not have a passphrase";
      break;
    case Security::WpaPsk:
    case Security::WpaPskSae:
      if (!passphraseOk(psk)) return "passphrase must be 8..63 printable characters or 64 hex digits";
      break;
    case Security::Sae:
      if (psk.empty() || psk.size() > 128) return "SAE password must be 1..128 characters";
      for (unsigned char c : psk)
        if (c < 0x20 || c == 0x7f) return "SAE password contains control characters";
      break;
    case Security::WpaEap:
      if (identity.empty()) return "802.1X network needs an identity";
      if (password.empty()) return "802.1X network needs a password";
      if (eap != "PEAP" && eap != "TTLS") return "eap must be PEAP or TTLS";
      break;
    case Security::Wep:
    case Security::Unknown: return "unsupported security type";
  }
  return std::nullopt;
}

const char* toString(ConnState s) {
  switch (s) {
    case ConnState::Unavailable: return "unavailable";
    case ConnState::Idle: return "idle";
    case ConnState::Scanning: return "scanning";
    case ConnState::Connecting: return "connecting";
    case ConnState::ObtainingIp: return "obtaining_ip";
    case ConnState::Connected: return "connected";
  }
  return "unknown";
}

}  // namespace wifimgr
