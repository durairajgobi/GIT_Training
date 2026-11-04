// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace blemgr {

/// Single-threaded event loop abstraction. All BleManager and backend logic
/// runs on the loop thread; post() is the only thread-safe entry point.
class IScheduler {
public:
    using TimerId = uint64_t;  // 0 is never a valid id
    using Clock = std::chrono::steady_clock;

    virtual ~IScheduler() = default;

    /// Run fn on the loop thread as soon as possible. Thread-safe.
    virtual void post(std::function<void()> fn) = 0;
    /// Run fn once after delay. Loop thread only.
    virtual TimerId postDelayed(std::chrono::milliseconds delay, std::function<void()> fn) = 0;
    /// Cancel a pending timer; unknown / fired ids are ignored. Loop thread only.
    virtual void cancel(TimerId id) = 0;
    virtual Clock::time_point now() const = 0;
};

}  // namespace blemgr
