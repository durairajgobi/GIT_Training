#include "wifimgr/wpa_parser.hpp"

#include "wifimgr/strutil.hpp"

namespace wifimgr {

Security securityFromFlags(std::string_view flags) {
  auto has = [&](std::string_view s) { return flags.find(s) != std::string_view::npos; };
  if (has("EAP")) return Security::WpaEap;
  bool psk = has("-PSK") || has("+PSK");
  bool sae = has("SAE");
  if (psk && sae) return Security::WpaPskSae;
  if (sae) return Security::Sae;
  if (psk) return Security::WpaPsk;
  if (has("WEP")) return Security::Wep;
  if (has("OWE") || has("WPA")) return Security::Unknown;
  return Security::Open;
}

std::vector<ScanResult> parseScanResults(std::string_view reply) {
  std::vector<ScanResult> out;
  for (std::string_view line : split(reply, '\n')) {
    if (line.empty() || startsWith(line, "bssid /")) continue;
    auto f = split(line, '\t');
    if (f.size() < 4) continue;
    ScanResult r;
    r.bssid = std::string(f[0]);
    auto freq = parseInt(f[1]);
    auto sig = parseInt(f[2]);
    if (!freq || !sig) continue;
    r.frequency = static_cast<int>(*freq);
    r.rssi = static_cast<int>(*sig);
    r.flags = std::string(f[3]);
    if (f.size() >= 5) {
      // The SSID is the last field and may itself contain an (escaped) tab;
      // join the remainder to be safe.
      std::string_view ssid = line.substr(f[0].size() + f[1].size() + f[2].size() + f[3].size() + 4);
      r.ssid = wpaDecode(ssid);
    }
    r.security = securityFromFlags(r.flags);
    out.push_back(std::move(r));
  }
  return out;
}

std::map<std::string, std::string> parseKeyValues(std::string_view reply) {
  std::map<std::string, std::string> out;
  for (std::string_view line : split(reply, '\n')) {
    auto eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    out[std::string(line.substr(0, eq))] = std::string(line.substr(eq + 1));
  }
  return out;
}

std::string eventField(std::string_view msg, std::string_view key) {
  std::string needle = std::string(key) + "=";
  size_t pos = 0;
  for (;;) {
    pos = msg.find(needle, pos);
    if (pos == std::string_view::npos) return {};
    if (pos == 0 || msg[pos - 1] == ' ') break;  // whole-word match only
    pos += needle.size();
  }
  size_t v = pos + needle.size();
  if (v < msg.size() && msg[v] == '"') {
    // Quoted (escaped with printf_encode); find the closing unescaped quote.
    size_t end = v + 1;
    while (end < msg.size() && msg[end] != '"') end += (msg[end] == '\\') ? 2 : 1;
    if (end > msg.size()) end = msg.size();
    return wpaDecode(msg.substr(v + 1, end - v - 1));
  }
  size_t end = msg.find(' ', v);
  return std::string(msg.substr(v, end == std::string_view::npos ? end : end - v));
}

SupplicantEvent parseEvent(std::string_view msg) {
  SupplicantEvent ev;
  if (startsWith(msg, "<")) {
    size_t gt = msg.find('>');
    if (gt != std::string_view::npos) msg.remove_prefix(gt + 1);
  }
  ev.raw = std::string(msg);
  using T = SupplicantEvent::Type;

  if (startsWith(msg, "CTRL-EVENT-SCAN-RESULTS")) {
    ev.type = T::ScanResults;
  } else if (startsWith(msg, "CTRL-EVENT-SCAN-FAILED")) {
    ev.type = T::ScanFailed;
    ev.reason = eventField(msg, "ret");
  } else if (startsWith(msg, "CTRL-EVENT-CONNECTED")) {
    // "CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:55 completed [id=0 id_str=]"
    ev.type = T::Connected;
    constexpr std::string_view kTo = "Connection to ";
    size_t p = msg.find(kTo);
    if (p != std::string_view::npos) ev.bssid = std::string(msg.substr(p + kTo.size(), 17));
  } else if (startsWith(msg, "CTRL-EVENT-DISCONNECTED")) {
    ev.type = T::Disconnected;
    ev.bssid = eventField(msg, "bssid");
    ev.reason = eventField(msg, "reason");
    ev.locallyGenerated = eventField(msg, "locally_generated") == "1";
  } else if (startsWith(msg, "CTRL-EVENT-SSID-TEMP-DISABLED")) {
    ev.ssid = eventField(msg, "ssid");
    ev.reason = eventField(msg, "reason");
    ev.type = (ev.reason == "WRONG_KEY" || ev.reason == "AUTH_FAILED") ? T::AuthFailure
                                                                       : T::ConnFailed;
  } else if (startsWith(msg, "CTRL-EVENT-EAP-FAILURE")) {
    ev.type = T::AuthFailure;
    ev.reason = "EAP_FAILURE";
  } else if (startsWith(msg, "CTRL-EVENT-ASSOC-REJECT") ||
             startsWith(msg, "CTRL-EVENT-AUTH-REJECT")) {
    ev.type = T::ConnFailed;
    ev.bssid = eventField(msg, "bssid");
    ev.reason = eventField(msg, "status_code");
  } else if (startsWith(msg, "CTRL-EVENT-NETWORK-NOT-FOUND")) {
    ev.type = T::NetworkNotFound;
  } else if (startsWith(msg, "CTRL-EVENT-TERMINATING")) {
    ev.type = T::Terminating;
  }
  return ev;
}

}  // namespace wifimgr
