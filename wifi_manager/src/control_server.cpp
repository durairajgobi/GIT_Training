#include "wifimgr/control_server.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "wifimgr/event_loop.hpp"
#include "wifimgr/log.hpp"
#include "wifimgr/strutil.hpp"

namespace wifimgr {

ControlServer::ControlServer(EventLoop& loop, std::string path, mode_t mode, Handler handler)
    : loop_(loop),
      path_(std::move(path)),
      mode_(mode),
      handler_(std::move(handler)),
      self_(std::make_shared<ControlServer*>(this)) {}

ControlServer::~ControlServer() {
  *self_ = nullptr;
  while (!clients_.empty()) drop(clients_.begin()->first);
  if (listenFd_ >= 0) {
    loop_.removeFd(listenFd_);
    close(listenFd_);
    unlink(path_.c_str());
  }
}

bool ControlServer::start(std::string* err) {
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path_.size() >= sizeof addr.sun_path) {
    *err = "control socket path too long";
    return false;
  }
  std::memcpy(addr.sun_path, path_.c_str(), path_.size());

  // Refuse to steal the socket of a running instance; remove a stale one.
  int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe >= 0) {
    bool live = connect(probe, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
    close(probe);
    if (live) {
      *err = path_ + " is in use by another instance";
      return false;
    }
  }
  unlink(path_.c_str());

  listenFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listenFd_ < 0) {
    *err = std::string("socket: ") + std::strerror(errno);
    return false;
  }
  // Create with restrictive umask, then widen to the configured mode.
  mode_t old = umask(0177);
  int rc = bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
  umask(old);
  if (rc < 0 || chmod(path_.c_str(), mode_) < 0 || listen(listenFd_, 8) < 0) {
    *err = path_ + ": " + std::strerror(errno);
    close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  return loop_.addFd(listenFd_, EPOLLIN, [this](uint32_t) { onAccept(); });
}

void ControlServer::onAccept() {
  for (;;) {
    int fd = accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return;
    if (clients_.size() >= kMaxClients) {
      LOGW("control: too many clients, rejecting");
      close(fd);
      continue;
    }
    ucred cred{};
    socklen_t len = sizeof cred;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
      close(fd);
      continue;
    }
    uint64_t id = nextId_++;
    Client& c = clients_[id];
    c.fd = fd;
    c.uid = cred.uid;
    loop_.addFd(fd, EPOLLIN | EPOLLRDHUP, [this, id](uint32_t ev) { onClient(id, ev); });
  }
}

void ControlServer::onClient(uint64_t id, uint32_t events) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  if (events & EPOLLOUT) flush(id);
  if (!(events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR))) return;

  char buf[1024];
  for (;;) {
    it = clients_.find(id);
    if (it == clients_.end()) return;
    ssize_t n = read(it->second.fd, buf, sizeof buf);
    if (n == 0) {
      drop(id);
      return;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR) break;
      drop(id);
      return;
    }
    it->second.in.append(buf, static_cast<size_t>(n));
    size_t nl;
    while ((it = clients_.find(id)) != clients_.end() &&
           (nl = it->second.in.find('\n')) != std::string::npos) {
      std::string line = it->second.in.substr(0, nl);
      it->second.in.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (trim(line).empty()) continue;

      Request req;
      req.client = id;
      req.uid = it->second.uid;
      auto toks = tokenize(line);
      std::weak_ptr<ControlServer*> weak = self_;
      Responder respond = [weak, id](const Json& reply) {
        if (auto s = weak.lock(); s && *s) (*s)->send(id, reply.dump());
      };
      if (!toks) {
        respond(Json::object().set("ok", false).set("error", "unterminated quote"));
        continue;
      }
      req.args = std::move(*toks);
      handler_(req, std::move(respond));
    }
    it = clients_.find(id);
    if (it != clients_.end() && it->second.in.size() > kMaxLine) {
      LOGW("control: request line too long, dropping client");
      drop(id);
      return;
    }
  }
}

void ControlServer::send(uint64_t id, const std::string& line) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  if (it->second.out.size() + line.size() > kMaxOutput) {
    LOGW("control: client not reading, dropping it");
    drop(id);
    return;
  }
  it->second.out += line;
  it->second.out += '\n';
  flush(id);
}

void ControlServer::flush(uint64_t id) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  Client& c = it->second;
  while (!c.out.empty()) {
    ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EAGAIN) break;
      if (errno == EINTR) continue;
      drop(id);
      return;
    }
    c.out.erase(0, static_cast<size_t>(n));
  }
  loop_.modifyFd(c.fd, EPOLLIN | EPOLLRDHUP | (c.out.empty() ? 0u : static_cast<uint32_t>(EPOLLOUT)));
}

void ControlServer::drop(uint64_t id) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  loop_.removeFd(it->second.fd);
  close(it->second.fd);
  clients_.erase(it);
}

void ControlServer::subscribe(uint64_t client) {
  auto it = clients_.find(client);
  if (it != clients_.end()) it->second.monitor = true;
}

void ControlServer::broadcast(const Json& event) {
  std::string line = event.dump();
  std::vector<uint64_t> targets;
  for (const auto& [id, c] : clients_)
    if (c.monitor) targets.push_back(id);
  for (uint64_t id : targets) send(id, line);
}

}  // namespace wifimgr
