// SPDX-License-Identifier: MIT
// Exponential backoff with "equal jitter" so a fleet of units that lose the
// same AP do not all reconnect in lock-step.
#pragma once

#include <cstdint>

namespace wifi6 {

class Backoff {
public:
    Backoff(uint32_t baseMs, uint32_t maxMs, uint32_t seed) noexcept
        : baseMs_(baseMs ? baseMs : 1), maxMs_(maxMs < baseMs_ ? baseMs_ : maxMs), rng_(seed ? seed : 0x9E3779B9u) {}

    // Delay for the next attempt: uniform in [exp/2, exp], exp = min(base*2^n, max).
    uint32_t next() noexcept {
        uint64_t exp = static_cast<uint64_t>(baseMs_) << (attempts_ < 20 ? attempts_ : 20);
        if (exp > maxMs_) exp = maxMs_;
        if (attempts_ < 0xFFFFu) ++attempts_;
        const uint32_t half = static_cast<uint32_t>(exp / 2);
        const uint32_t span = static_cast<uint32_t>(exp) - half;
        return half + (nextRandom() % (span + 1));
    }

    void reset() noexcept { attempts_ = 0; }
    uint32_t attempts() const noexcept { return attempts_; }

private:
    uint32_t nextRandom() noexcept {  // xorshift32
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    uint32_t baseMs_;
    uint32_t maxMs_;
    uint32_t rng_;
    uint32_t attempts_ = 0;
};

}  // namespace wifi6
