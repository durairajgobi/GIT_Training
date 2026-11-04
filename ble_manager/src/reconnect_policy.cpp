// SPDX-License-Identifier: MIT
#include "blemgr/reconnect_policy.hpp"

#include <algorithm>
#include <cmath>

namespace blemgr {

ReconnectPolicy::ReconnectPolicy(ReconnectConfig cfg, uint32_t seed) : cfg_(cfg), rng_(seed) {
    cfg_.multiplier = std::max(1.0, cfg_.multiplier);
    cfg_.jitter = std::clamp(cfg_.jitter, 0.0, 1.0);
    if (cfg_.maxDelay < cfg_.initialDelay) cfg_.maxDelay = cfg_.initialDelay;
}

std::optional<std::chrono::milliseconds> ReconnectPolicy::next() {
    if (cfg_.maxAttempts && attempts_ >= cfg_.maxAttempts) return std::nullopt;

    double base = static_cast<double>(cfg_.initialDelay.count()) *
                  std::pow(cfg_.multiplier, static_cast<double>(std::min<uint32_t>(attempts_, 63)));
    base = std::min(base, static_cast<double>(cfg_.maxDelay.count()));
    ++attempts_;

    if (cfg_.jitter > 0) {
        std::uniform_real_distribution<double> dist(-cfg_.jitter, cfg_.jitter);
        base *= 1.0 + dist(rng_);
    }
    return std::chrono::milliseconds(static_cast<int64_t>(std::max(0.0, std::round(base))));
}

}  // namespace blemgr
