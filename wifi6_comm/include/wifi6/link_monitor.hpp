// SPDX-License-Identifier: MIT
// Smoothed link quality tracking with hysteresis, integer-only (no FPU
// required). RSSI is filtered with an EWMA (alpha = 1/8) in Q4 fixed point.
#pragma once

#include <cstdint>

#include "wifi6/types.hpp"

namespace wifi6 {

class LinkMonitor {
public:
    explicit LinkMonitor(int16_t weakThresholdDbm = -72, int16_t hysteresisDb = 4) noexcept
        : weakDbm_(weakThresholdDbm), hystDb_(hysteresisDb) {}

    void reset() noexcept;
    void update(const LinkStats& s) noexcept;

    int16_t smoothedRssi() const noexcept { return static_cast<int16_t>(ewmaQ4_ / 16); }
    bool isWeak() const noexcept { return weak_; }
    uint32_t samples() const noexcept { return samples_; }
    const LinkStats& last() const noexcept { return last_; }

private:
    int16_t weakDbm_;
    int16_t hystDb_;
    int32_t ewmaQ4_ = 0;
    bool weak_ = false;
    uint32_t samples_ = 0;
    LinkStats last_{};
};

}  // namespace wifi6
