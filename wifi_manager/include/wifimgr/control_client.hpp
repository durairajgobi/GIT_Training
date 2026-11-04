// Blocking client for the control socket (used by wifictl).
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace wifimgr {

class ControlClient {
 public:
  ~ControlClient();
  bool connect(const std::string& path, std::string* err);
  // Sends args as one quoted request line.
  bool send(const std::vector<std::string>& args);
  // Reads one reply line; timeoutMs < 0 waits forever.
  std::optional<std::string> readLine(int timeoutMs);

 private:
  int fd_ = -1;
  std::string buf_;
};

}  // namespace wifimgr
