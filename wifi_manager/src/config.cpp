#include "wifimgr/config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>

#include "wifimgr/strutil.hpp"

namespace wifimgr {
namespace {

bool parseSeconds(const std::string& v, Millis* out, long minS, long maxS, std::string* err) {
  auto n = parseInt(v);
  if (!n || *n < minS || *n > maxS) {
    *err = "expected seconds in " + std::to_string(minS) + ".." + std::to_string(maxS);
    return false;
  }
  *out = Millis(*n * 1000);
  return true;
}

bool parseRange(const std::string& v, int* out, long lo, long hi, std::string* err) {
  auto n = parseInt(v);
  if (!n || *n < lo || *n > hi) {
    *err = "expected integer in " + std::to_string(lo) + ".." + std::to_string(hi);
    return false;
  }
  *out = static_cast<int>(*n);
  return true;
}

bool parsePath(const std::string& v, std::string* out, std::string* err) {
  if (v.empty() || v[0] != '/') {
    *err = "expected an absolute path";
    return false;
  }
  *out = v;
  return true;
}

}  // namespace

bool isValidInterfaceName(const std::string& name) {
  // Also guards the {iface} substitution into the DHCP shell command.
  if (name.empty() || name.size() > 15) return false;
  for (unsigned char c : name)
    if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') return false;
  return true;
}

bool applyConfigValue(DaemonConfig& cfg, const std::string& key, const std::string& value,
                      std::string* err) {
  ManagerConfig& m = cfg.manager;
  if (key == "interface") {
    if (!isValidInterfaceName(value)) {
      *err = "invalid interface name";
      return false;
    }
    cfg.interface = value;
    return true;
  }
  if (key == "wpa_ctrl_dir") return parsePath(value, &cfg.wpaCtrlDir, err);
  if (key == "wpa_client_dir") return parsePath(value, &cfg.wpaClientDir, err);
  if (key == "control_socket") return parsePath(value, &cfg.controlSocket, err);
  if (key == "profiles") return parsePath(value, &cfg.profiles, err);
  if (key == "control_socket_mode") {
    char* end = nullptr;
    long mode = std::strtol(value.c_str(), &end, 8);
    if (value.empty() || *end != '\0' || mode < 0 || mode > 0777) {
      *err = "expected an octal mode like 0660";
      return false;
    }
    cfg.controlSocketMode = static_cast<mode_t>(mode);
    return true;
  }
  if (key == "dhcp_command") {
    cfg.dhcpCommand = value;
    return true;
  }
  if (key == "netlink") {
    if (parseBool(value, &cfg.netlink)) return true;
    *err = "expected a boolean";
    return false;
  }
  if (key == "syslog") {
    if (parseBool(value, &cfg.syslog)) return true;
    *err = "expected a boolean";
    return false;
  }
  if (key == "log_level") {
    auto l = logLevelFromString(value);
    if (!l) {
      *err = "expected debug, info, warn or error";
      return false;
    }
    cfg.logLevel = *l;
    return true;
  }
  if (key == "autoconnect") {
    if (parseBool(value, &m.autoconnect)) return true;
    *err = "expected a boolean";
    return false;
  }
  if (key == "min_rssi") return parseRange(value, &m.selection.minRssi, -120, 0, err);
  if (key == "band_5ghz_bonus") return parseRange(value, &m.selection.band5GhzBonus, 0, 30, err);
  if (key == "roam_rssi_threshold") return parseRange(value, &m.roamRssiThreshold, -120, 0, err);
  if (key == "roam_hysteresis") return parseRange(value, &m.roamHysteresis, 0, 50, err);
  if (key == "max_auth_failures") return parseRange(value, &m.maxAuthFailures, 1, 100, err);
  if (key == "scan_interval_min") return parseSeconds(value, &m.scanIntervalMin, 1, 3600, err);
  if (key == "scan_interval_max") return parseSeconds(value, &m.scanIntervalMax, 1, 86400, err);
  if (key == "scan_timeout") return parseSeconds(value, &m.scanTimeout, 1, 120, err);
  if (key == "connect_timeout") return parseSeconds(value, &m.connectTimeout, 1, 300, err);
  if (key == "dhcp_timeout") return parseSeconds(value, &m.dhcpTimeout, 1, 600, err);
  if (key == "signal_poll_interval")
    return parseSeconds(value, &m.signalPollInterval, 1, 3600, err);
  if (key == "bgscan_interval") return parseSeconds(value, &m.bgScanInterval, 0, 86400, err);
  if (key == "blacklist_base") return parseSeconds(value, &m.blacklistBase, 1, 3600, err);
  if (key == "blacklist_max") return parseSeconds(value, &m.blacklistMax, 1, 86400, err);
  *err = "unknown key";
  return false;
}

bool loadConfigFile(const std::string& path, DaemonConfig& cfg, std::vector<std::string>* errors) {
  std::ifstream in(path);
  if (!in) {
    errors->push_back(path + ": cannot open");
    return false;
  }
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    std::string_view t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    // Inline comment: '#' preceded by whitespace.
    for (size_t i = 1; i < t.size(); ++i) {
      if (t[i] == '#' && (t[i - 1] == ' ' || t[i - 1] == '\t')) {
        t = trim(t.substr(0, i));
        break;
      }
    }
    auto eq = t.find('=');
    if (eq == std::string_view::npos) {
      errors->push_back(path + ":" + std::to_string(lineNo) + ": expected key = value");
      continue;
    }
    std::string key(trim(t.substr(0, eq)));
    std::string value(trim(t.substr(eq + 1)));
    std::string err;
    if (!applyConfigValue(cfg, key, value, &err))
      errors->push_back(path + ":" + std::to_string(lineNo) + ": " + key + ": " + err);
  }
  if (cfg.manager.scanIntervalMax < cfg.manager.scanIntervalMin)
    errors->push_back(path + ": scan_interval_max is smaller than scan_interval_min");
  if (cfg.manager.blacklistMax < cfg.manager.blacklistBase)
    errors->push_back(path + ": blacklist_max is smaller than blacklist_base");
  return errors->empty();
}

}  // namespace wifimgr
