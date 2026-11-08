// SPDX-License-Identifier: MIT
// Candidate selection and roaming policy.
//
// Score = RSSI + band bonus + HE bonus. 6 GHz gets the largest bonus (clean
// spectrum, 160 MHz channels, no legacy clients) but only while the signal
// is strong enough - 6 GHz range is shorter, so a weak 6 GHz link loses its
// bonus and the station falls back to 5 GHz.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wifi6/types.hpp"

namespace wifi6 {

struct SelectorPolicy {
    int16_t band6Bonus = 20;
    int16_t band5Bonus = 15;
    int16_t heBonus = 4;
    int16_t band6MinRssiDbm = -72;  // below this 6 GHz gets no bonus
    int16_t band5MinRssiDbm = -75;
    int16_t roamTriggerDbm = -70;   // only look for a roam target below this
    int16_t roamDeltaDb = 8;        // hysteresis: candidate must be this much better
};

bool securityCompatible(Security ap, Security wanted) noexcept;

class ApSelector {
public:
    explicit ApSelector(const SelectorPolicy& p = SelectorPolicy{}) noexcept : p_(p) {}

    bool isEligible(const ApInfo& ap, const NetworkConfig& cfg) const noexcept;
    int32_t score(const ApInfo& ap) const noexcept;

    const ApInfo* selectBest(const ApInfo* aps, size_t n, const NetworkConfig& cfg,
                             const Bssid* exclude = nullptr) const noexcept;

    // Returns a better BSS of the same ESS, or nullptr if we should stay.
    const ApInfo* selectRoamTarget(const ApInfo* aps, size_t n, const NetworkConfig& cfg,
                                   const Bssid& current, int16_t currentRssiDbm) const noexcept;

    const SelectorPolicy& policy() const noexcept { return p_; }

private:
    SelectorPolicy p_;
};

}  // namespace wifi6
