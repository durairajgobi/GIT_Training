// SPDX-License-Identifier: MIT
// Station connection state machine.
//
//            start()
//   Idle ───────────► Scanning ──ScanDone/best AP──► Connecting ──Connected──► Connected
//    ▲                  │  ▲                            │                        │
//    │ stop()           │  │ backoff expired            │ fail/timeout           │ link lost
//    │                  ▼  │                            ▼                        ▼
//    └──────────────── Backoff ◄────────────────────────┴────────────────────────┘
//                         │ too many auth failures
//                         ▼
//                       Failed (wrong credentials - wait for new start())
//
// While Connected the manager polls link stats, runs a background scan when
// the smoothed RSSI turns weak, roams to a clearly better BSS and
// (re)negotiates a TWT agreement after every association.
//
// Single-threaded and non-blocking: drive it from an event loop with tick().
#pragma once

#include <array>
#include <cstdint>

#include "wifi6/ap_selector.hpp"
#include "wifi6/backoff.hpp"
#include "wifi6/link_monitor.hpp"
#include "wifi6/wifi_driver.hpp"

namespace wifi6 {

enum class ConnState : uint8_t { Idle, Scanning, Connecting, Connected, Backoff, Failed };
const char* toString(ConnState s) noexcept;

struct ConnectionPolicy {
    uint32_t scanTimeoutMs = 10000;
    uint32_t connectTimeoutMs = 20000;
    uint32_t statsPollMs = 1000;
    uint32_t bgScanMinIntervalMs = 30000;
    uint32_t backoffBaseMs = 1000;
    uint32_t backoffMaxMs = 60000;
    uint8_t maxAuthFailures = 3;
    bool enableTwt = false;
    uint32_t twtIntervalUs = 0;
    uint32_t twtWakeUs = 0;
    int16_t weakRssiDbm = -72;
    SelectorPolicy selector{};
};

class IConnectionObserver {
public:
    virtual ~IConnectionObserver() = default;
    virtual void onStateChanged(ConnState from, ConnState to, Status reason) noexcept = 0;
    virtual void onLinkStats(const LinkStats&) noexcept {}
    virtual void onRoamed(const ApInfo&) noexcept {}
};

class ConnectionManager {
public:
    ConnectionManager(IWifiDriver& driver, const ConnectionPolicy& policy,
                      IConnectionObserver* observer = nullptr) noexcept;

    Status start(const NetworkConfig& cfg, uint64_t nowMs) noexcept;
    void stop() noexcept;
    void tick(uint64_t nowMs) noexcept;

    ConnState state() const noexcept { return state_; }
    Status lastError() const noexcept { return lastError_; }
    const ApInfo& currentAp() const noexcept { return current_; }
    const LinkMonitor& link() const noexcept { return link_; }
    bool twtActive() const noexcept { return twtActive_; }
    uint32_t roamCount() const noexcept { return roams_; }

private:
    void handleEvent(const DriverEvent& ev, uint64_t now) noexcept;
    void enter(ConnState next, Status reason) noexcept;
    void beginScan(uint64_t now, bool background) noexcept;
    void onScanDone(uint64_t now) noexcept;
    void onConnected(const DriverEvent& ev, uint64_t now) noexcept;
    void scheduleBackoff(uint64_t now, Status reason) noexcept;
    void pollStats(uint64_t now) noexcept;
    void setupTwt() noexcept;
    const ApInfo* findScanned(const Bssid& b) const noexcept;

    IWifiDriver& driver_;
    ConnectionPolicy policy_;
    IConnectionObserver* observer_;
    ApSelector selector_;
    Backoff backoff_;
    LinkMonitor link_;

    NetworkConfig cfg_{};
    ConnState state_ = ConnState::Idle;
    Status lastError_ = Status::Ok;
    uint64_t deadline_ = 0;
    uint64_t lastNow_ = 0;
    uint64_t nextStatsMs_ = 0;
    uint64_t lastBgScanMs_ = 0;
    uint64_t bgScanStartMs_ = 0;
    uint64_t roamDeadline_ = 0;
    bool hasBgScanned_ = false;
    bool bgScanPending_ = false;
    bool roamPending_ = false;
    bool twtActive_ = false;
    bool needReopen_ = false;
    bool excludeValid_ = false;
    uint8_t authFailures_ = 0;
    uint32_t roams_ = 0;

    std::array<ApInfo, kMaxScanResults> scan_{};
    size_t scanCount_ = 0;
    ApInfo target_{};
    ApInfo roamTarget_{};
    ApInfo current_{};
    Bssid exclude_{};
};

}  // namespace wifi6
