#include "wifimgr/event_loop.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "wifimgr/log.hpp"

extern char** environ;

namespace wifimgr {

EventLoop::EventLoop() {
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0) LOGE("epoll_create1: %s", std::strerror(errno));
}

EventLoop::~EventLoop() {
  for (int fd : signalFds_) close(fd);
  if (epfd_ >= 0) close(epfd_);
}

bool EventLoop::addFd(int fd, uint32_t events, FdCallback cb) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
    LOGE("epoll add fd %d: %s", fd, std::strerror(errno));
    return false;
  }
  fds_[fd] = std::make_shared<FdCallback>(std::move(cb));
  return true;
}

bool EventLoop::modifyFd(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  return epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void EventLoop::removeFd(int fd) {
  if (fds_.erase(fd) > 0) epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
}

bool EventLoop::watchSignals(std::initializer_list<int> signals, SignalCallback cb) {
  sigset_t mask;
  sigemptyset(&mask);
  for (int s : signals) sigaddset(&mask, s);
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) return false;
  int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sfd < 0) {
    LOGE("signalfd: %s", std::strerror(errno));
    return false;
  }
  signalFds_.push_back(sfd);
  return addFd(sfd, EPOLLIN, [sfd, cb = std::move(cb)](uint32_t) {
    signalfd_siginfo si;
    while (read(sfd, &si, sizeof si) == static_cast<ssize_t>(sizeof si)) cb(si);
  });
}

bool EventLoop::watchChild(pid_t pid, ChildCallback cb) {
  if (!childWatchInstalled_) {
    if (!watchSignals({SIGCHLD}, [this](const signalfd_siginfo&) { reapChildren(); }))
      return false;
    childWatchInstalled_ = true;
  }
  children_[pid] = std::move(cb);
  // The child may already have exited before we started watching.
  reapChildren();
  return true;
}

void EventLoop::unwatchChild(pid_t pid) { children_.erase(pid); }

void EventLoop::reapChildren() {
  // Only reap pids we own so other code using waitpid() is not disturbed.
  for (auto it = children_.begin(); it != children_.end();) {
    int status = 0;
    pid_t r = waitpid(it->first, &status, WNOHANG);
    if (r == it->first || (r < 0 && errno == ECHILD)) {
      auto cb = std::move(it->second);
      pid_t pid = it->first;
      it = children_.erase(it);
      if (cb) cb(pid, r < 0 ? 0 : status);
    } else {
      ++it;
    }
  }
}

ITimers::TimerId EventLoop::startTimer(Millis delay, std::function<void()> cb) {
  TimerId id = nextTimerId_++;
  TimePoint deadline = Clock::now() + delay;
  timers_[id] = TimerEntry{deadline, std::move(cb)};
  timerQueue_.insert({deadline, id});
  return id;
}

void EventLoop::cancelTimer(TimerId id) {
  auto it = timers_.find(id);
  if (it == timers_.end()) return;
  timerQueue_.erase({it->second.deadline, id});
  timers_.erase(it);
}

int EventLoop::nextTimeoutMs() const {
  if (timerQueue_.empty()) return -1;
  auto delta = timerQueue_.begin()->first - Clock::now();
  if (delta <= Clock::duration::zero()) return 0;
  // Round up so we never wake before the deadline and spin.
  auto ms = std::chrono::duration_cast<Millis>(delta + Millis(1) - Clock::duration(1)).count();
  return ms > 60000 ? 60000 : static_cast<int>(ms);
}

void EventLoop::fireTimers() {
  TimePoint now = Clock::now();
  while (!timerQueue_.empty() && timerQueue_.begin()->first <= now) {
    TimerId id = timerQueue_.begin()->second;
    timerQueue_.erase(timerQueue_.begin());
    auto it = timers_.find(id);
    if (it == timers_.end()) continue;
    auto cb = std::move(it->second.cb);
    timers_.erase(it);
    cb();
  }
}

void EventLoop::runOnce(int timeoutMs) {
  int t = nextTimeoutMs();
  if (t < 0 || (timeoutMs >= 0 && timeoutMs < t)) t = timeoutMs;
  epoll_event events[16];
  int n = epoll_wait(epfd_, events, 16, t);
  if (n < 0 && errno != EINTR) LOGE("epoll_wait: %s", std::strerror(errno));
  for (int i = 0; i < n; ++i) {
    auto it = fds_.find(events[i].data.fd);
    if (it == fds_.end()) continue;  // removed by an earlier callback
    auto cb = it->second;            // keep alive across removeFd() inside cb
    (*cb)(events[i].events);
  }
  fireTimers();
}

int EventLoop::run() {
  running_ = true;
  while (running_) runOnce(-1);
  return exitCode_;
}

void EventLoop::stop(int exitCode) {
  exitCode_ = exitCode;
  running_ = false;
}

pid_t spawnShell(const std::string& command, const std::vector<std::string>& env) {
  pid_t pid = fork();
  if (pid < 0) {
    LOGE("fork: %s", std::strerror(errno));
    return -1;
  }
  if (pid > 0) return pid;

  // Child: own process group so the whole pipeline can be signalled.
  setsid();
  sigset_t empty;
  sigemptyset(&empty);
  sigprocmask(SIG_SETMASK, &empty, nullptr);
  for (int s = 1; s < NSIG; ++s) signal(s, SIG_DFL);
  for (const auto& kv : env) putenv(const_cast<char*>(kv.c_str()));
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull >= 0) {
    dup2(devnull, STDIN_FILENO);
    close(devnull);
  }
  execle("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr), environ);
  _exit(127);
}

}  // namespace wifimgr
