// Time source and one-shot timers. The event loop implements ITimers for the
// daemon; unit tests use a manually advanced fake.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace wifimgr {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Millis = std::chrono::milliseconds;

class ITimers {
 public:
  using TimerId = uint64_t;  // 0 is never a valid id
  virtual ~ITimers() = default;
  virtual TimePoint now() const = 0;
  virtual TimerId startTimer(Millis delay, std::function<void()> cb) = 0;
  virtual void cancelTimer(TimerId id) = 0;
};

// RAII single-shot timer handle. Restarting cancels the pending expiry.
class Timer {
 public:
  explicit Timer(ITimers& timers) : timers_(timers) {}
  ~Timer() { cancel(); }
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

  void start(Millis delay, std::function<void()> cb) {
    cancel();
    id_ = timers_.startTimer(delay, [this, cb = std::move(cb)] {
      id_ = 0;
      cb();
    });
  }
  void cancel() {
    if (id_ != 0) {
      timers_.cancelTimer(id_);
      id_ = 0;
    }
  }
  bool active() const { return id_ != 0; }

 private:
  ITimers& timers_;
  ITimers::TimerId id_ = 0;
};

}  // namespace wifimgr
