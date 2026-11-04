// Single-threaded epoll reactor: file descriptors, timers, signals and child
// process reaping. Every component of the daemon runs on this one thread,
// so no locking is needed anywhere.
#pragma once

#include <sys/signalfd.h>
#include <sys/types.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "wifimgr/timers.hpp"

namespace wifimgr {

class EventLoop : public ITimers {
 public:
  using FdCallback = std::function<void(uint32_t events)>;
  using SignalCallback = std::function<void(const signalfd_siginfo&)>;
  using ChildCallback = std::function<void(pid_t pid, int waitStatus)>;

  EventLoop();
  ~EventLoop() override;
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  bool addFd(int fd, uint32_t events, FdCallback cb);
  bool modifyFd(int fd, uint32_t events);
  void removeFd(int fd);

  // Blocks the given signals and delivers them through a signalfd.
  bool watchSignals(std::initializer_list<int> signals, SignalCallback cb);

  // Calls cb once when `pid` exits. Installs SIGCHLD handling on first use.
  // Children must reset their signal mask before exec (see spawnShell()).
  bool watchChild(pid_t pid, ChildCallback cb);
  void unwatchChild(pid_t pid);

  int run();
  void stop(int exitCode = 0);
  // Runs one iteration with the given max wait; used by tests.
  void runOnce(int timeoutMs);

  TimePoint now() const override { return Clock::now(); }
  TimerId startTimer(Millis delay, std::function<void()> cb) override;
  void cancelTimer(TimerId id) override;

 private:
  int nextTimeoutMs() const;
  void fireTimers();
  void reapChildren();

  int epfd_ = -1;
  bool running_ = false;
  int exitCode_ = 0;
  std::unordered_map<int, std::shared_ptr<FdCallback>> fds_;
  std::vector<int> signalFds_;

  struct TimerEntry {
    TimePoint deadline;
    std::function<void()> cb;
  };
  TimerId nextTimerId_ = 1;
  std::unordered_map<TimerId, TimerEntry> timers_;
  std::set<std::pair<TimePoint, TimerId>> timerQueue_;

  bool childWatchInstalled_ = false;
  std::map<pid_t, ChildCallback> children_;
};

// fork()+exec("/bin/sh -c cmd") in a new session with default signal
// dispositions and an unblocked signal mask. Returns the pid or -1.
pid_t spawnShell(const std::string& command, const std::vector<std::string>& env = {});

}  // namespace wifimgr
