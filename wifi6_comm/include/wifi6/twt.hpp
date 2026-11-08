// SPDX-License-Identifier: MIT
// 802.11ax individual Target Wake Time (TWT) agreement parameters.
//
// The wake interval is signalled as  mantissa * 2^exponent  microseconds
// (16-bit mantissa, 5-bit exponent). The nominal minimum wake duration is
// in units of 256 us. TWT lets a battery-powered station sleep between
// scheduled service periods instead of waking for every beacon.
#pragma once

#include <cstdint>

#include "wifi6/status.hpp"

namespace wifi6 {

enum class TwtSetupCmd : uint8_t { Request = 0, Suggest = 1, Demand = 2 };

struct TwtParams {
    uint8_t flowId = 0;            // 3-bit flow identifier
    uint8_t dialogToken = 1;
    uint8_t exponent = 0;          // 0..31
    uint16_t mantissa = 0;
    uint8_t minWakeDuration = 0;   // units of 256 us
    TwtSetupCmd setupCmd = TwtSetupCmd::Suggest;
    bool trigger = true;           // trigger-enabled service period
    bool implicit = true;          // implicit (periodic) agreement
    bool announced = true;         // STA announces itself (PS-Poll/QoS-Null) at SP start

    uint64_t intervalUs() const noexcept {
        return static_cast<uint64_t>(mantissa) << exponent;
    }
    uint32_t wakeDurationUs() const noexcept { return static_cast<uint32_t>(minWakeDuration) * 256u; }

    // Encode a desired interval/duration. The duration is rounded up to the
    // next 256 us unit; the interval is represented with the smallest
    // exponent that keeps the mantissa within 16 bits.
    static Status fromMicros(uint64_t intervalUs, uint32_t wakeDurationUs, TwtParams& out) noexcept;
};

}  // namespace wifi6
