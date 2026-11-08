// SPDX-License-Identifier: MIT
#include "wifi6/link_monitor.hpp"

namespace wifi6 {

void LinkMonitor::reset() noexcept {
    ewmaQ4_ = 0;
    weak_ = false;
    samples_ = 0;
    last_ = LinkStats{};
}

void LinkMonitor::update(const LinkStats& s) noexcept {
    last_ = s;
    const int32_t sampleQ4 = static_cast<int32_t>(s.rssiDbm) * 16;
    if (samples_ == 0)
        ewmaQ4_ = sampleQ4;
    else
        ewmaQ4_ += (sampleQ4 - ewmaQ4_) / 8;
    if (samples_ < 0xFFFFFFFFu) ++samples_;

    const int16_t rssi = smoothedRssi();
    if (!weak_ && rssi < weakDbm_)
        weak_ = true;
    else if (weak_ && rssi > weakDbm_ + hystDb_)
        weak_ = false;
}

}  // namespace wifi6
