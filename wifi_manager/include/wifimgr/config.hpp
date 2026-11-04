// Daemon configuration (/etc/wifimgr/wifimgr.conf, key = value).
#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

#include "wifimgr/connection_manager.hpp"
#include "wifimgr/log.hpp"

namespace wifimgr {

struct DaemonConfig {
  std::string interface = "wlan0";
  std::string wpaCtrlDir = "/run/wpa_supplicant";
  std::string wpaClientDir = "/run/wifimgr";
  std::string controlSocket = "/run/wifimgr/wifimgr.sock";
  mode_t controlSocketMode = 0660;
  std::string profiles = "/var/lib/wifimgr/networks.conf";
  // Empty disables DHCP (address must then be configured by something else
  // and is picked up through netlink).
  std::string dhcpCommand =
      "udhcpc -f -R -t 4 -T 3 -A 10 -i {iface} -s /usr/libexec/wifimgr/udhcpc-hook";
  bool netlink = true;
  LogLevel logLevel = LogLevel::Info;
  bool syslog = false;
  ManagerConfig manager;
};

// Applies one `key = value` setting. Returns false with *err on bad input.
bool applyConfigValue(DaemonConfig& cfg, const std::string& key, const std::string& value,
                      std::string* err);
// Loads a file on top of `cfg`. Errors are fatal (refuse to start with a
// config we do not understand), each reported with its line number.
bool loadConfigFile(const std::string& path, DaemonConfig& cfg, std::vector<std::string>* errors);

bool isValidInterfaceName(const std::string& name);

}  // namespace wifimgr
