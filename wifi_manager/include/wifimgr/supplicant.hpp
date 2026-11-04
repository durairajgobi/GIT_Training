// Abstraction over wpa_supplicant used by the connection manager, and the
// production implementation talking to its control socket.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "wifimgr/timers.hpp"
#include "wifimgr/types.hpp"
#include "wifimgr/wpa_ctrl.hpp"
#include "wifimgr/wpa_parser.hpp"

namespace wifimgr {

class EventLoop;

class ISupplicant {
 public:
  using EventHandler = std::function<void(const SupplicantEvent&)>;
  using AvailabilityHandler = std::function<void(bool available)>;
  virtual ~ISupplicant() = default;

  virtual void setEventHandler(EventHandler h) = 0;
  virtual void setAvailabilityHandler(AvailabilityHandler h) = 0;
  virtual bool available() const = 0;

  // Requests a scan; SSIDs of hidden profiles are probed explicitly.
  virtual bool scan(const std::vector<std::string>& hiddenSsids) = 0;
  virtual std::vector<ScanResult> scanResults() = 0;
  // Replaces whatever network wpa_supplicant has with `p` and selects it.
  virtual bool connect(const Profile& p) = 0;
  // Disconnects and removes all networks so wpa_supplicant does not
  // reconnect on its own.
  virtual bool disconnect() = 0;
  virtual std::optional<LinkInfo> linkInfo() = 0;
};

class WpaSupplicant : public ISupplicant {
 public:
  WpaSupplicant(EventLoop& loop, std::string iface, std::string ctrlDir, std::string clientDir);
  ~WpaSupplicant() override;

  // Connects now if possible, otherwise keeps retrying in the background.
  void start();

  void setEventHandler(EventHandler h) override { onEvent_ = std::move(h); }
  void setAvailabilityHandler(AvailabilityHandler h) override { onAvail_ = std::move(h); }
  bool available() const override { return available_; }

  bool scan(const std::vector<std::string>& hiddenSsids) override;
  std::vector<ScanResult> scanResults() override;
  bool connect(const Profile& p) override;
  bool disconnect() override;
  std::optional<LinkInfo> linkInfo() override;

 private:
  bool tryOpen();
  void markLost(const char* why);
  void onEventsReadable();
  void healthCheck();
  // Sends a command; `secret` suppresses logging of the arguments.
  std::optional<std::string> cmd(const std::string& c, bool secret = false);
  bool cmdOk(const std::string& c, bool secret = false);
  bool configureNetwork(const std::string& id, const Profile& p);

  EventLoop& loop_;
  std::string iface_;
  std::string ctrlPath_;
  std::string clientDir_;
  WpaCtrl ctrl_;    // request/reply
  WpaCtrl events_;  // ATTACHed for unsolicited events
  bool available_ = false;
  bool warnedUnavailable_ = false;
  Timer retryTimer_;
  Timer healthTimer_;
  EventHandler onEvent_;
  AvailabilityHandler onAvail_;
};

// wpa_supplicant config-file string literal: "..." for printable ASCII
// without quotes, otherwise a hex string (no quotes).
std::string wpaStringValue(const std::string& s);

}  // namespace wifimgr
