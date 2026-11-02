// tsn.hpp - Compile-time TSN schedules and wrappers over the C shapers.
//
// A gate control list is part of the system's timing contract, so it is
// built and checked at compile time:
//
//   constexpr auto gcl = epp::tsn::makeGcl({
//       {0x80, 100'000},   // TC7 exclusive: 100 us
//       {0x7F, 400'000},   // everything else
//   });
//   static_assert(gcl.cycleTime() == 500'000);
//   static_assert(gcl.maxGateClosed(7) <= 400'000);   // worst-case wait
#pragma once

#include <array>

#include "epp/core.hpp"
#include "ep/ep_eth.h"
#include "ep/ep_tsn.h"

namespace epp {
namespace tsn {

struct GateEntry {
    std::uint8_t gates;
    std::uint32_t interval_ns;
    ep_gcl_op_t op = EP_GCL_SET_GATES;
};

template <std::size_t N>
class GateControlList {
public:
    constexpr explicit GateControlList(const GateEntry (&list)[N])
    {
        for (std::size_t i = 0; i < N; i++) {
            e_[i].op = static_cast<std::uint8_t>(list[i].op);
            e_[i].gate_mask = list[i].gates;
            e_[i].interval_ns = list[i].interval_ns;
        }
    }

    constexpr std::size_t size() const { return N; }
    constexpr const ep_tsn_gcl_entry_t &operator[](std::size_t i) const { return e_[i]; }
    const ep_tsn_gcl_entry_t *data() const { return e_.data(); }

    constexpr std::uint64_t cycleTime() const
    {
        std::uint64_t s = 0;
        for (std::size_t i = 0; i < N; i++)
            s += e_[i].interval_ns;
        return s;
    }

    // Total time per cycle the gate of traffic class tc is open.
    constexpr std::uint64_t openTime(std::uint8_t tc) const
    {
        std::uint64_t s = 0;
        for (std::size_t i = 0; i < N; i++)
            if (e_[i].gate_mask & (1u << tc))
                s += e_[i].interval_ns;
        return s;
    }

    // Longest continuous closed period for tc (wrapping across cycles):
    // the worst-case wait before a frame of that class may start.
    constexpr std::uint64_t maxGateClosed(std::uint8_t tc) const
    {
        if (openTime(tc) == 0)
            return ~0ull;
        std::uint64_t best = 0, run = 0;
        for (std::size_t k = 0; k < 2 * N; k++) {
            const auto &e = e_[k % N];
            if (e.gate_mask & (1u << tc)) {
                run = 0;
            } else {
                run += e.interval_ns;
                if (run > best)
                    best = run;
            }
        }
        return best;
    }

    // Bandwidth share of tc in parts per million of the link.
    constexpr std::uint32_t shareppm(std::uint8_t tc) const
    {
        return static_cast<std::uint32_t>(openTime(tc) * 1000000ull / cycleTime());
    }

private:
    std::array<ep_tsn_gcl_entry_t, N> e_{};
};

template <std::size_t N>
constexpr GateControlList<N> makeGcl(const GateEntry (&list)[N])
{
    return GateControlList<N>(list);
}

// Largest frame (bytes, without FCS) that fits a window of `ns` at `bps`.
constexpr std::size_t maxFrameInWindow(std::uint64_t ns, std::uint64_t bps)
{
    const std::uint64_t overhead = EP_ETH_FCS_LEN + EP_ETH_PREAMBLE_SFD + EP_ETH_IFG;
    std::uint64_t bytes = ns * bps / 8 / 1000000000ull;
    return bytes > overhead ? static_cast<std::size_t>(bytes - overhead) : 0;
}

class TimeAwareShaper {
public:
    template <std::size_t N>
    TimeAwareShaper(const GateControlList<N> &gcl, std::uint64_t base_ns, std::uint64_t link_bps)
    {
        init_ = toStatus(ep_tsn_tas_init(&tas_, gcl.data(), N, base_ns, 0, link_bps));
    }
    Status initStatus() const { return init_; }
    std::uint8_t gates(std::uint64_t now) const { return ep_tsn_tas_gates(&tas_, now, nullptr, nullptr); }
    bool canTransmit(std::uint64_t now, std::uint8_t tc, std::size_t len) const
    {
        return ep_tsn_tas_can_tx(&tas_, now, tc, len);
    }
    std::uint64_t nextTransmit(std::uint64_t now, std::uint8_t tc, std::size_t len) const
    {
        return ep_tsn_tas_next_tx(&tas_, now, tc, len);
    }
    const ep_tsn_tas_t &raw() const { return tas_; }

private:
    ep_tsn_tas_t tas_{};
    Status init_ = Status::State;
};

class CreditBasedShaper {
public:
    CreditBasedShaper(std::uint64_t idle_slope_bps, std::uint64_t port_bps, std::uint32_t max_frame = 1522,
                      std::uint32_t max_interference = 1522)
    {
        init_ = toStatus(ep_tsn_cbs_init(&c_, idle_slope_bps, port_bps, max_frame, max_interference));
    }
    Status initStatus() const { return init_; }
    std::int64_t hiCredit() const { return c_.hi_credit_bits; }
    std::int64_t loCredit() const { return c_.lo_credit_bits; }
    std::int64_t credit() const { return ep_tsn_cbs_credit_bits(&c_); }
    ep_tsn_cbs_t &raw() { return c_; }

private:
    ep_tsn_cbs_t c_{};
    Status init_ = Status::State;
};

class SequenceRecovery {
public:
    explicit SequenceRecovery(std::uint16_t history = 32, std::uint64_t reset_timeout_ns = 0)
    {
        ep_tsn_frer_init(&r_, history, reset_timeout_ns);
    }
    bool accept(std::uint16_t seq, std::uint64_t now = 0) { return ep_tsn_frer_accept(&r_, seq, now); }
    const ep_tsn_frer_t &stats() const { return r_; }

private:
    ep_tsn_frer_t r_{};
};

}  // namespace tsn
}  // namespace epp
