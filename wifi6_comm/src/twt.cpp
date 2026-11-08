// SPDX-License-Identifier: MIT
#include "wifi6/twt.hpp"

namespace wifi6 {

Status TwtParams::fromMicros(uint64_t intervalUs, uint32_t wakeDurationUs, TwtParams& out) noexcept {
    if (intervalUs == 0 || wakeDurationUs == 0) return Status::InvalidArg;

    const uint32_t units = (wakeDurationUs + 255u) / 256u;
    if (units > 0xFFu) return Status::InvalidArg;  // > 65.28 ms not representable

    for (uint8_t e = 0; e <= 31; ++e) {
        const uint64_t half = e ? (uint64_t{1} << (e - 1)) : 0;
        const uint64_t mant = (intervalUs + half) >> e;  // round to nearest
        if (mant == 0) return Status::InvalidArg;
        if (mant > 0xFFFFu) continue;

        TwtParams p = out;  // keep caller's flow id / flags
        p.exponent = e;
        p.mantissa = static_cast<uint16_t>(mant);
        p.minWakeDuration = static_cast<uint8_t>(units);
        if (p.wakeDurationUs() >= p.intervalUs()) return Status::InvalidArg;
        out = p;
        return Status::Ok;
    }
    return Status::InvalidArg;
}

}  // namespace wifi6
