// SPDX-License-Identifier: MIT
// IScheduler backed by a private GMainContext running on its own thread.
// The BlueZ backend must be started on this thread (BleManager does that), so
// all D-Bus signals and method calls are dispatched here as well.
#pragma once

#include <memory>

#include "blemgr/scheduler.hpp"

namespace blemgr {

class GlibScheduler final : public IScheduler {
public:
    GlibScheduler();
    ~GlibScheduler() override;  // stops the loop if still running

    void start();  // spawns the loop thread
    void stop();   // quits the loop and joins; pending tasks are dropped

    void post(std::function<void()> fn) override;
    TimerId postDelayed(std::chrono::milliseconds delay, std::function<void()> fn) override;
    void cancel(TimerId id) override;
    Clock::time_point now() const override { return Clock::now(); }

    bool inLoopThread() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace blemgr
