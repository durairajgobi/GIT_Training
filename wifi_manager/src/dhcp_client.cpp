#include "wifimgr/dhcp_client.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "wifimgr/event_loop.hpp"
#include "wifimgr/log.hpp"

namespace wifimgr {
namespace {

std::string expand(std::string tmpl, const std::string& iface) {
  const std::string key = "{iface}";
  for (size_t p; (p = tmpl.find(key)) != std::string::npos;) tmpl.replace(p, key.size(), iface);
  return tmpl;
}

}  // namespace

ProcessDhcpClient::ProcessDhcpClient(EventLoop& loop, std::string commandTemplate,
                                     std::string iface)
    : loop_(loop),
      command_(expand(std::move(commandTemplate), iface)),
      iface_(std::move(iface)) {}

ProcessDhcpClient::~ProcessDhcpClient() {
  // Shutdown path: terminate synchronously so no orphan keeps renewing a
  // lease for an interface nobody manages any more.
  if (pid_ > 0) stopping_.insert(pid_);
  for (pid_t pid : stopping_) {
    loop_.unwatchChild(pid);
    kill(-pid, SIGTERM);
    bool reaped = false;
    for (int i = 0; i < 20 && !reaped; ++i) {
      reaped = waitpid(pid, nullptr, WNOHANG) == pid;
      if (!reaped) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!reaped) {
      kill(-pid, SIGKILL);
      waitpid(pid, nullptr, 0);
    }
  }
}

bool ProcessDhcpClient::start() {
  if (pid_ > 0) return true;
  pid_t pid = spawnShell(command_, {"WIFIMGR_IFACE=" + iface_});
  if (pid < 0) return false;
  pid_ = pid;
  LOGI("dhcp: started '%s' (pid %d)", command_.c_str(), static_cast<int>(pid));
  loop_.watchChild(pid, [this](pid_t p, int st) { onChildExit(p, st); });
  return true;
}

void ProcessDhcpClient::stop() {
  if (pid_ <= 0) return;
  pid_t pid = pid_;
  pid_ = -1;  // from now on its exit is expected and not reported
  LOGI("dhcp: stopping pid %d", static_cast<int>(pid));
  stopping_.insert(pid);
  kill(-pid, SIGTERM);  // whole process group (the client and its hook)
  // Escalate if it ignores SIGTERM. The child stays watched so it is reaped.
  loop_.startTimer(Millis(3000), [this, pid] {
    if (stopping_.count(pid)) kill(-pid, SIGKILL);
  });
}

void ProcessDhcpClient::onChildExit(pid_t pid, int status) {
  int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  if (stopping_.erase(pid) > 0) return;  // a process we asked to stop
  pid_ = -1;
  LOGW("dhcp: client exited unexpectedly (status %d)", code);
  if (onExit_) onExit_(code);
}

}  // namespace wifimgr
