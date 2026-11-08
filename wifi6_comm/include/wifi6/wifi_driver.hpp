// SPDX-License-Identifier: MIT
// Hardware abstraction for the Wi-Fi station. The connection manager only
// talks to this interface, so it runs unchanged against wpa_supplicant on
// target hardware or against the simulator in host unit tests.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wifi6/status.hpp"
#include "wifi6/twt.hpp"
#include "wifi6/types.hpp"

namespace wifi6 {

enum class DriverEventType : uint8_t {
    ScanDone,
    ScanFailed,
    Connected,         // association + 4-way handshake complete
    Disconnected,
    AuthFailed,        // wrong passphrase / SAE commit rejected
    ConnectFailed,     // association rejected or other non-credential failure
    NetworkNotFound,
    SignalChange,
    DriverTerminated,  // supplicant / driver went away
};

struct DriverEvent {
    DriverEventType type = DriverEventType::ScanDone;
    Bssid bssid{};
    int16_t rssiDbm = 0;
    uint16_t reason = 0;  // 802.11 reason code where applicable
};

class IWifiDriver {
public:
    virtual ~IWifiDriver() = default;

    virtual Status open() noexcept = 0;
    virtual void close() noexcept = 0;

    virtual Status startScan() noexcept = 0;
    virtual Status scanResults(ApInfo* out, size_t cap, size_t& count) noexcept = 0;

    // Join `cfg`; if `pinned` is non-null, restrict association to that BSS.
    virtual Status connect(const NetworkConfig& cfg, const Bssid* pinned) noexcept = 0;
    virtual Status disconnect() noexcept = 0;
    virtual Status roam(const Bssid& target) noexcept = 0;

    // Non-blocking. Returns Status::WouldBlock when no event is queued.
    virtual Status pollEvent(DriverEvent& ev) noexcept = 0;
    virtual Status linkStats(LinkStats& out) noexcept = 0;

    virtual Status setupTwt(const TwtParams& params) noexcept = 0;
    virtual Status teardownTwt(uint8_t flowId) noexcept = 0;

    // File descriptor that becomes readable when events are pending, or -1.
    virtual int eventFd() const noexcept { return -1; }
};

}  // namespace wifi6
