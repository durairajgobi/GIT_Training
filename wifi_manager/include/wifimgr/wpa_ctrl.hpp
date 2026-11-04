// Client for wpa_supplicant's UNIX datagram control interface
// (ctrl_interface=DIR=/run/wpa_supplicant). Re-implemented instead of
// linking libwpa_client so the daemon has no build dependency on hostap.
#pragma once

#include <optional>
#include <string>

namespace wifimgr {

class WpaCtrl {
 public:
  WpaCtrl() = default;
  ~WpaCtrl();
  WpaCtrl(const WpaCtrl&) = delete;
  WpaCtrl& operator=(const WpaCtrl&) = delete;

  // ctrlPath: e.g. /run/wpa_supplicant/wlan0. localDir: where to bind our
  // reply socket; wpa_supplicant must be able to sendto() it.
  bool open(const std::string& ctrlPath, const std::string& localDir);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  int fd() const { return fd_; }

  // Sends a command and waits for its reply. Unsolicited "<N>..." messages
  // that arrive in between are skipped. nullopt on timeout or socket error.
  std::optional<std::string> request(const std::string& cmd, int timeoutMs = 5000);

  // Registers this socket for unsolicited events.
  bool attach();

  enum class RecvStatus { Message, WouldBlock, Error };
  // Non-blocking read of one pending datagram.
  RecvStatus receive(std::string* msg);

 private:
  int fd_ = -1;
  std::string localPath_;
};

}  // namespace wifimgr
