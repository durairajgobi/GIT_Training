// SPDX-License-Identifier: MIT
// Deterministic in-memory Wi-Fi driver for host unit tests and demo mode.
// Events are queued synchronously and delivered on the next pollEvent().
#pragma once

#include <array>

#include "wifi6/ring_buffer.hpp"
#include "wifi6/wifi_driver.hpp"

namespace wifi6 {

class SimDriver final : public IWifiDriver {
public:
    static constexpr size_t kMaxAps = 16;

    // Scenario control.
    bool addAp(const ApInfo& ap) noexcept;
    void setApRssi(const Bssid& bssid, int16_t rssiDbm) noexcept;
    void setPassphrase(const char* pass) noexcept;
    void setTwtSupported(bool supported) noexcept { twtSupported_ = supported; }
    void setScanFails(bool fails) noexcept { scanFails_ = fails; }
    void dropLink(uint16_t reason = 4 /* inactivity */) noexcept;
    void terminate() noexcept;

    // Inspection.
    bool isConnected() const noexcept { return connected_; }
    const Bssid& connectedBssid() const noexcept { return cur_; }
    bool twtActive() const noexcept { return twtActive_; }
    const TwtParams& lastTwt() const noexcept { return twt_; }
    uint32_t connectAttempts() const noexcept { return connectAttempts_; }
    uint32_t roamCount() const noexcept { return roamCount_; }
    uint32_t scanCount() const noexcept { return scanCount_; }

    // IWifiDriver
    Status open() noexcept override;
    void close() noexcept override;
    Status startScan() noexcept override;
    Status scanResults(ApInfo* out, size_t cap, size_t& count) noexcept override;
    Status connect(const NetworkConfig& cfg, const Bssid* pinned) noexcept override;
    Status disconnect() noexcept override;
    Status roam(const Bssid& target) noexcept override;
    Status pollEvent(DriverEvent& ev) noexcept override;
    Status linkStats(LinkStats& out) noexcept override;
    Status setupTwt(const TwtParams& params) noexcept override;
    Status teardownTwt(uint8_t flowId) noexcept override;

private:
    ApInfo* find(const Bssid& b) noexcept;
    void push(DriverEventType type, const Bssid& b = Bssid{}, uint16_t reason = 0) noexcept;

    std::array<ApInfo, kMaxAps> aps_{};
    size_t apCount_ = 0;
    SpscRing<DriverEvent, 64> events_;
    char pass_[kMaxPassphraseLen + 1]{};
    Bssid cur_{};
    TwtParams twt_{};
    bool open_ = false;
    bool connected_ = false;
    bool twtSupported_ = true;
    bool twtActive_ = false;
    bool scanFails_ = false;
    uint32_t connectAttempts_ = 0;
    uint32_t roamCount_ = 0;
    uint32_t scanCount_ = 0;
};

}  // namespace wifi6
