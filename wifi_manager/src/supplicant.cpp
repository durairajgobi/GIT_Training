#include "wifimgr/supplicant.hpp"

#include <sys/epoll.h>

#include "wifimgr/event_loop.hpp"
#include "wifimgr/log.hpp"
#include "wifimgr/strutil.hpp"

namespace wifimgr {
namespace {

constexpr Millis kRetryInterval{2000};
constexpr Millis kHealthInterval{10000};

}  // namespace

std::string wpaStringValue(const std::string& s) {
  for (unsigned char c : s)
    if (c < 0x20 || c > 0x7e || c == '"') return hexEncode(s);
  return "\"" + s + "\"";
}

WpaSupplicant::WpaSupplicant(EventLoop& loop, std::string iface, std::string ctrlDir,
                             std::string clientDir)
    : loop_(loop),
      iface_(std::move(iface)),
      ctrlPath_(std::move(ctrlDir) + "/" + iface_),
      clientDir_(std::move(clientDir)),
      retryTimer_(loop),
      healthTimer_(loop) {}

WpaSupplicant::~WpaSupplicant() {
  if (events_.isOpen()) {
    loop_.removeFd(events_.fd());
    events_.request("DETACH", 500);
  }
}

void WpaSupplicant::start() {
  if (!tryOpen()) retryTimer_.start(kRetryInterval, [this] { start(); });
}

bool WpaSupplicant::tryOpen() {
  if (!ctrl_.open(ctrlPath_, clientDir_) || !events_.open(ctrlPath_, clientDir_)) {
    ctrl_.close();
    events_.close();
    if (!warnedUnavailable_) {
      LOGW("wpa_supplicant control socket %s not reachable, retrying", ctrlPath_.c_str());
      warnedUnavailable_ = true;
    }
    return false;
  }
  auto pong = ctrl_.request("PING", 2000);
  if (!pong || pong->rfind("PONG", 0) != 0 || !events_.attach()) {
    ctrl_.close();
    events_.close();
    return false;
  }
  loop_.addFd(events_.fd(), EPOLLIN, [this](uint32_t) { onEventsReadable(); });
  available_ = true;
  warnedUnavailable_ = false;
  LOGI("connected to wpa_supplicant on %s", iface_.c_str());
  // We own network selection: start from a clean slate.
  cmdOk("REMOVE_NETWORK all");
  healthTimer_.start(kHealthInterval, [this] { healthCheck(); });
  if (onAvail_) onAvail_(true);
  return true;
}

void WpaSupplicant::markLost(const char* why) {
  if (!available_) return;
  LOGW("lost wpa_supplicant (%s)", why);
  if (events_.isOpen()) loop_.removeFd(events_.fd());
  events_.close();
  ctrl_.close();
  available_ = false;
  healthTimer_.cancel();
  if (onAvail_) onAvail_(false);
  retryTimer_.start(kRetryInterval, [this] { start(); });
}

void WpaSupplicant::healthCheck() {
  // SIGKILLed supplicants never send TERMINATING; a PING catches that.
  auto r = ctrl_.request("PING", 2000);
  if (!r || r->rfind("PONG", 0) != 0) {
    markLost("PING timeout");
    return;
  }
  healthTimer_.start(kHealthInterval, [this] { healthCheck(); });
}

void WpaSupplicant::onEventsReadable() {
  for (;;) {
    std::string msg;
    auto st = events_.receive(&msg);
    if (st == WpaCtrl::RecvStatus::WouldBlock) return;
    if (st == WpaCtrl::RecvStatus::Error) {
      markLost("event socket error");
      return;
    }
    SupplicantEvent ev = parseEvent(msg);
    LOGD("wpa event: %s", ev.raw.c_str());
    if (onEvent_) onEvent_(ev);
    if (ev.type == SupplicantEvent::Type::Terminating) {
      markLost("terminating");
      return;
    }
  }
}

std::optional<std::string> WpaSupplicant::cmd(const std::string& c, bool secret) {
  if (!available_) return std::nullopt;
  auto r = ctrl_.request(c);
  if (!r) {
    LOGW("wpa_supplicant: no reply to %s", secret ? "<redacted>" : c.c_str());
    markLost("request timeout");
    return std::nullopt;
  }
  while (!r->empty() && r->back() == '\n') r->pop_back();
  LOGD("wpa> %s -> %s", secret ? "<redacted>" : c.c_str(), r->size() > 80 ? "..." : r->c_str());
  return r;
}

bool WpaSupplicant::cmdOk(const std::string& c, bool secret) {
  auto r = cmd(c, secret);
  return r && *r == "OK";
}

bool WpaSupplicant::scan(const std::vector<std::string>& hiddenSsids) {
  std::string c = "SCAN";
  for (const auto& s : hiddenSsids) c += " ssid " + hexEncode(s);
  auto r = cmd(c);
  // FAIL-BUSY means a scan is already running; its results will do.
  return r && (*r == "OK" || *r == "FAIL-BUSY");
}

std::vector<ScanResult> WpaSupplicant::scanResults() {
  auto r = cmd("SCAN_RESULTS");
  return r ? parseScanResults(*r) : std::vector<ScanResult>{};
}

bool WpaSupplicant::configureNetwork(const std::string& id, const Profile& p) {
  auto set = [&](const std::string& key, const std::string& value, bool secret = false) {
    if (cmdOk("SET_NETWORK " + id + " " + key + " " + value, secret)) return true;
    LOGW("wpa_supplicant rejected network parameter %s", key.c_str());
    return false;
  };
  if (!set("ssid", hexEncode(p.ssid))) return false;
  if (p.hidden && !set("scan_ssid", "1")) return false;
  if (!set("priority", std::to_string(p.priority))) return false;

  // A 64 hex digit WPA-PSK is a raw key and goes in unquoted. Passphrases are
  // always quoted: wpa_supplicant's psk parser treats unquoted text as a raw
  // key, and takes everything up to the *last* quote, so embedded quotes are
  // fine (validate() already limits passphrases to printable ASCII).
  std::string psk = (p.psk.size() == 64 && isHexString(p.psk)) ? p.psk : "\"" + p.psk + "\"";
  switch (p.security) {
    case Security::Open:
      return set("key_mgmt", "NONE");
    case Security::WpaPsk:
      return set("key_mgmt", "WPA-PSK") && set("ieee80211w", "1") && set("psk", psk, true);
    case Security::Sae:
      return set("key_mgmt", "SAE") && set("ieee80211w", "2") &&
             set("sae_password", wpaStringValue(p.psk), true);
    case Security::WpaPskSae:
      return set("key_mgmt", "WPA-PSK SAE") && set("ieee80211w", "1") && set("psk", psk, true) &&
             set("sae_password", wpaStringValue(p.psk), true);
    case Security::WpaEap:
      if (!set("key_mgmt", "WPA-EAP") || !set("ieee80211w", "1") || !set("eap", p.eap) ||
          !set("identity", wpaStringValue(p.identity)) ||
          !set("password", wpaStringValue(p.password), true) ||
          !set("phase2", wpaStringValue(p.phase2)))
        return false;
      if (!p.anonymousIdentity.empty() &&
          !set("anonymous_identity", wpaStringValue(p.anonymousIdentity)))
        return false;
      if (p.caCert.empty()) {
        LOGW("802.1X network has no ca_cert: server certificate is NOT validated");
        return true;
      }
      return set("ca_cert", wpaStringValue(p.caCert));
    case Security::Wep:
    case Security::Unknown:
      return false;
  }
  return false;
}

bool WpaSupplicant::connect(const Profile& p) {
  if (!cmdOk("REMOVE_NETWORK all")) return false;
  auto id = cmd("ADD_NETWORK");
  if (!id || !parseInt(*id)) return false;
  if (!configureNetwork(*id, p) || !cmdOk("SELECT_NETWORK " + *id)) {
    cmdOk("REMOVE_NETWORK all");
    return false;
  }
  return true;
}

bool WpaSupplicant::disconnect() {
  bool ok = cmdOk("DISCONNECT");
  return cmdOk("REMOVE_NETWORK all") && ok;
}

std::optional<LinkInfo> WpaSupplicant::linkInfo() {
  auto st = cmd("STATUS");
  if (!st) return std::nullopt;
  auto kv = parseKeyValues(*st);
  LinkInfo li;
  li.wpaState = kv["wpa_state"];
  li.bssid = kv["bssid"];
  li.ssid = wpaDecode(kv["ssid"]);
  li.ipAddress = kv["ip_address"];
  if (auto f = parseInt(kv["freq"])) li.frequency = static_cast<int>(*f);
  if (li.wpaState == "COMPLETED") {
    if (auto sp = cmd("SIGNAL_POLL")) {
      auto s = parseKeyValues(*sp);
      if (auto v = parseInt(s["RSSI"])) li.rssi = static_cast<int>(*v);
      if (auto v = parseInt(s["LINKSPEED"])) li.linkSpeedMbps = static_cast<int>(*v);
      if (auto v = parseInt(s["FREQUENCY"])) li.frequency = static_cast<int>(*v);
    }
  }
  return li;
}

}  // namespace wifimgr
