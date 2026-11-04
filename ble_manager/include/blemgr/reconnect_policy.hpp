// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>

namespace blemgr {

struct ReconnectConfig {
    std::chrono::milliseconds initialDelay{500};
    std::chrono::milliseconds maxDelay{30000};
    double multiplier = 2.0;
    double jitter = 0.2;       // +/- fraction applied to each delay
    uint32_t maxAttempts = 0;  // 0 = retry forever
};

/// Exponential back-off with bounded random jitter.
class ReconnectPolicy {
public:
    explicit ReconnectPolicy(ReconnectConfig cfg = {}, uint32_t seed = 1);

    /// Delay before the next attempt, or nullopt once maxAttempts is exhausted.
    std::optional<std::chrono::milliseconds> next();
    void reset() { attempts_ = 0; }
    uint32_t attempts() const { return attempts_; }

private:
    ReconnectConfig cfg_;
    uint32_t attempts_ = 0;
    std::mt19937 rng_;
};

}  // namespace blemgr
