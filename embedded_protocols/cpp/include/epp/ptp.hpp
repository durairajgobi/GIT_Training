// ptp.hpp - C++ value types and helpers for IEEE 1588 / 802.1AS.
#pragma once

#include <algorithm>
#include <array>

#include "epp/core.hpp"
#include "ep/ep_ptp.h"

namespace epp {
namespace ptp {

// PTP timestamp (TAI): 48-bit seconds + nanoseconds, with exact arithmetic.
class Timestamp {
public:
    constexpr Timestamp() = default;
    constexpr Timestamp(std::uint64_t sec, std::uint32_t nsec) : ns_(static_cast<std::int64_t>(sec) * 1000000000LL + nsec) {}
    static constexpr Timestamp fromNs(std::int64_t ns) { return Timestamp(NsTag{}, ns); }

    constexpr std::int64_t ns() const { return ns_; }
    constexpr std::uint64_t seconds() const { return static_cast<std::uint64_t>(ns_ / 1000000000LL); }
    constexpr std::uint32_t nanoseconds() const { return static_cast<std::uint32_t>(ns_ % 1000000000LL); }

    constexpr std::int64_t operator-(Timestamp o) const { return ns_ - o.ns_; }
    constexpr Timestamp operator+(std::int64_t d) const { return fromNs(ns_ + d); }
    constexpr Timestamp operator-(std::int64_t d) const { return fromNs(ns_ - d); }
    constexpr bool operator<(Timestamp o) const { return ns_ < o.ns_; }
    constexpr bool operator==(Timestamp o) const { return ns_ == o.ns_; }
    constexpr bool operator!=(Timestamp o) const { return ns_ != o.ns_; }

    void pack(std::uint8_t out[10]) const { ep_ptp_ts_pack(out, ns_); }
    static Timestamp unpack(const std::uint8_t in[10]) { return fromNs(ep_ptp_ts_unpack(in)); }

private:
    struct NsTag {};
    constexpr Timestamp(NsTag, std::int64_t ns) : ns_(ns) {}
    std::int64_t ns_ = 0;
};

// Correction field (scaled nanoseconds, 2^-16 ns resolution).
constexpr std::int64_t toCorrection(std::int64_t ns) { return ns * 65536; }
constexpr double correctionToNs(std::int64_t corr) { return static_cast<double>(corr) / 65536.0; }

// Sliding median: rejects path-delay outliers (queueing in non-PTP switches).
template <class T, std::size_t N>
class MedianFilter {
    static_assert(N >= 1 && N <= 64, "window 1..64");

public:
    T push(T v)
    {
        win_[idx_] = v;
        idx_ = (idx_ + 1) % N;
        if (n_ < N)
            n_++;
        std::array<T, N> tmp{};
        std::copy(win_.begin(), win_.begin() + static_cast<std::ptrdiff_t>(n_), tmp.begin());
        auto mid = tmp.begin() + static_cast<std::ptrdiff_t>(n_ / 2);
        std::nth_element(tmp.begin(), mid, tmp.begin() + static_cast<std::ptrdiff_t>(n_));
        if (n_ % 2)
            return *mid;
        T hi = *mid;
        T lo = *std::max_element(tmp.begin(), mid);
        return static_cast<T>(lo + (hi - lo) / 2);
    }
    std::size_t size() const { return n_; }
    void reset() { n_ = idx_ = 0; }

private:
    std::array<T, N> win_{};
    std::size_t idx_ = 0, n_ = 0;
};

// RAII wrapper over the C PI servo.
class PiServo {
public:
    PiServo(double kp = 0.7, double ki = 0.3, double max_ppb = 500000.0) { ep_ptp_servo_init(&s_, kp, ki, max_ppb); }
    void setStepThreshold(std::int64_t ns) { s_.step_threshold_ns = ns; }
    void setFirstStepThreshold(std::int64_t ns) { s_.first_step_threshold_ns = ns; }

    struct Output {
        double ppb;
        ep_servo_state_t state;
    };
    Output sample(std::int64_t offset_ns, std::uint64_t local_ts_ns)
    {
        Output o{};
        o.ppb = ep_ptp_servo_sample(&s_, offset_ns, local_ts_ns, &o.state);
        return o;
    }
    double drift() const { return -s_.drift_ppb; }
    void reset() { ep_ptp_servo_reset(&s_); }

private:
    ep_ptp_servo_t s_{};
};

}  // namespace ptp
}  // namespace epp
