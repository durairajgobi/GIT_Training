#include "wifimgr/wpa_ctrl.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>

#include "wifimgr/log.hpp"

namespace wifimgr {
namespace {

std::atomic<unsigned> g_counter{0};
constexpr size_t kMaxReply = 16384;

bool fillAddr(sockaddr_un* addr, const std::string& path) {
  std::memset(addr, 0, sizeof *addr);
  addr->sun_family = AF_UNIX;
  if (path.size() >= sizeof addr->sun_path) return false;
  std::memcpy(addr->sun_path, path.data(), path.size());
  return true;
}

}  // namespace

WpaCtrl::~WpaCtrl() { close(); }

bool WpaCtrl::open(const std::string& ctrlPath, const std::string& localDir) {
  close();
  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return false;

  std::string local = localDir + "/wifimgr_" + std::to_string(getpid()) + "-" +
                      std::to_string(g_counter++);
  sockaddr_un addr;
  if (!fillAddr(&addr, local)) {
    ::close(fd);
    return false;
  }
  unlink(local.c_str());  // stale socket from a crashed previous instance
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
    LOGW("wpa_ctrl: bind %s: %s", local.c_str(), std::strerror(errno));
    ::close(fd);
    return false;
  }

  sockaddr_un dest;
  if (!fillAddr(&dest, ctrlPath) ||
      connect(fd, reinterpret_cast<sockaddr*>(&dest), sizeof dest) < 0) {
    LOGD("wpa_ctrl: connect %s: %s", ctrlPath.c_str(), std::strerror(errno));
    ::close(fd);
    unlink(local.c_str());
    return false;
  }
  fd_ = fd;
  localPath_ = local;
  return true;
}

void WpaCtrl::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (!localPath_.empty()) {
    unlink(localPath_.c_str());
    localPath_.clear();
  }
}

std::optional<std::string> WpaCtrl::request(const std::string& cmd, int timeoutMs) {
  if (fd_ < 0) return std::nullopt;
  if (send(fd_, cmd.data(), cmd.size(), 0) < 0) {
    LOGD("wpa_ctrl: send: %s", std::strerror(errno));
    return std::nullopt;
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                    .count();
    if (left <= 0) return std::nullopt;
    pollfd pfd{fd_, POLLIN, 0};
    int r = poll(&pfd, 1, static_cast<int>(left));
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return std::nullopt;
    std::string msg;
    switch (receive(&msg)) {
      case RecvStatus::WouldBlock: continue;
      case RecvStatus::Error: return std::nullopt;
      case RecvStatus::Message: break;
    }
    if (!msg.empty() && msg[0] == '<') continue;  // unsolicited event
    return msg;
  }
}

bool WpaCtrl::attach() {
  auto r = request("ATTACH");
  return r && r->rfind("OK", 0) == 0;
}

WpaCtrl::RecvStatus WpaCtrl::receive(std::string* msg) {
  char buf[kMaxReply];
  ssize_t n = recv(fd_, buf, sizeof buf, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return RecvStatus::WouldBlock;
    return RecvStatus::Error;
  }
  msg->assign(buf, static_cast<size_t>(n));
  return RecvStatus::Message;
}

}  // namespace wifimgr
