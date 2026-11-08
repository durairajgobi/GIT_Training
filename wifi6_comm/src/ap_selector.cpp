// SPDX-License-Identifier: MIT
#include "wifi6/ap_selector.hpp"

#include <cstring>

namespace wifi6 {

bool securityCompatible(Security ap, Security wanted) noexcept {
    switch (wanted) {
        case Security::Open: return ap == Security::Open;
        case Security::Owe: return ap == Security::Owe;
        case Security::Wpa2Psk: return ap == Security::Wpa2Psk || ap == Security::Wpa2Wpa3;
        case Security::Wpa3Sae: return ap == Security::Wpa3Sae || ap == Security::Wpa2Wpa3;
        case Security::Wpa2Wpa3:
            return ap == Security::Wpa2Psk || ap == Security::Wpa3Sae || ap == Security::Wpa2Wpa3;
        case Security::Enterprise:
        case Security::Unsupported: break;
    }
    return false;
}

bool ApSelector::isEligible(const ApInfo& ap, const NetworkConfig& cfg) const noexcept {
    if (std::strncmp(ap.ssid, cfg.ssid, sizeof ap.ssid) != 0) return false;
    if (!securityCompatible(ap.security, cfg.security)) return false;
    if (ap.rssiDbm < cfg.minRssiDbm) return false;
    const Band b = ap.band();
    if (!cfg.bandAllowed(b)) return false;
    // 6 GHz forbids legacy security: only SAE / OWE (Wi-Fi 6E requirement).
    if (b == Band::Ghz6 && cfg.security != Security::Wpa3Sae && cfg.security != Security::Owe &&
        cfg.security != Security::Wpa2Wpa3)
        return false;
    return true;
}

int32_t ApSelector::score(const ApInfo& ap) const noexcept {
    int32_t s = ap.rssiDbm;
    switch (ap.band()) {
        case Band::Ghz6:
            if (ap.rssiDbm >= p_.band6MinRssiDbm) s += p_.band6Bonus;
            break;
        case Band::Ghz5:
            if (ap.rssiDbm >= p_.band5MinRssiDbm) s += p_.band5Bonus;
            break;
        default: break;
    }
    if (ap.heCapable) s += p_.heBonus;
    return s;
}

const ApInfo* ApSelector::selectBest(const ApInfo* aps, size_t n, const NetworkConfig& cfg,
                                     const Bssid* exclude) const noexcept {
    const ApInfo* best = nullptr;
    int32_t bestScore = 0;
    for (size_t i = 0; i < n; ++i) {
        const ApInfo& ap = aps[i];
        if (exclude && ap.bssid == *exclude) continue;
        if (!isEligible(ap, cfg)) continue;
        const int32_t sc = score(ap);
        if (!best || sc > bestScore) {
            best = &ap;
            bestScore = sc;
        }
    }
    return best;
}

const ApInfo* ApSelector::selectRoamTarget(const ApInfo* aps, size_t n, const NetworkConfig& cfg,
                                           const Bssid& current, int16_t currentRssiDbm) const noexcept {
    if (currentRssiDbm >= p_.roamTriggerDbm) return nullptr;

    ApInfo cur{};
    cur.rssiDbm = currentRssiDbm;
    for (size_t i = 0; i < n; ++i) {
        if (aps[i].bssid == current) {
            cur = aps[i];
            cur.rssiDbm = currentRssiDbm;  // trust our smoothed value over one scan sample
            break;
        }
    }
    const int32_t curScore = score(cur);

    const ApInfo* best = selectBest(aps, n, cfg, &current);
    if (!best || score(*best) < curScore + p_.roamDeltaDb) return nullptr;
    return best;
}

}  // namespace wifi6
