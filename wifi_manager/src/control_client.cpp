#include "wifimgr/control_client.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#include "wifimgr/strutil.hpp"

namespace wifimgr {

ControlClient::~ControlClient() {
  if (fd_ >= 0) close(fd_);
}

bool ControlClient::connect(const std::string& path, std::string* err) {
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof addr.sun_path) {
    *err = "socket path too long";
    return false;
  }
  std::memcpy(addr.sun_path, path.c_str(), path.size());
  fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd_ < 0 || ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
    *err = path + ": " + std::strerror(errno);
    return false;
  }
  return true;
}

bool ControlClient::send(const std::vector<std::string>& args) {
  std::string line;
  for (const auto& a : args) {
    if (!line.empty()) line.push_back(' ');
    line += quoteArg(a);
  }
  line.push_back('\n');
  size_t off = 0;
  while (off < line.size()) {
    ssize_t n = ::send(fd_, line.data() + off, line.size() - off, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

std::optional<std::string> ControlClient::readLine(int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    size_t nl = buf_.find('\n');
    if (nl != std::string::npos) {
      std::string line = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      return line;
    }
    int wait = -1;
    if (timeoutMs >= 0) {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                      deadline - std::chrono::steady_clock::now())
                      .count();
      if (left <= 0) return std::nullopt;
      wait = static_cast<int>(left);
    }
    pollfd pfd{fd_, POLLIN, 0};
    int r = poll(&pfd, 1, wait);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return std::nullopt;
    char tmp[4096];
    ssize_t n = read(fd_, tmp, sizeof tmp);
    if (n <= 0) return std::nullopt;
    buf_.append(tmp, static_cast<size_t>(n));
  }
}

}  // namespace wifimgr
