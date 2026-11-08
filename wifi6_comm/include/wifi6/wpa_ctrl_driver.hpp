// SPDX-License-Identifier: MIT
// IWifiDriver backed by the wpa_supplicant control interface
// (/var/run/wpa_supplicant/<iface>, UNIX datagram sockets). Works with any
// mac80211/nl80211 Wi-Fi 6 chipset driver (iwlwifi, ath11k, mt7921, brcmfmac,
// NXP mwifiex/moal, ...) that wpa_supplicant supports.
#pragma once

#include "wifi6/wifi_driver.hpp"

namespace wifi6 {

class WpaCtrlDriver final : public IWifiDriver {
public:
    WpaCtrlDriver(const char* ctrlDir, const char* iface) noexcept;
    ~WpaCtrlDriver() override;

    WpaCtrlDriver(const WpaCtrlDriver&) = delete;
    WpaCtrlDriver& operator=(const WpaCtrlDriver&) = delete;

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
    int eventFd() const noexcept override { return monFd_; }

private:
    static constexpr size_t kPathLen = 108;  // sizeof(sockaddr_un::sun_path)
    static constexpr size_t kReplyLen = 16384;
    static constexpr size_t kBssReplyLen = 4096;

    Status openSocket(int& fd, char* localPath) noexcept;
    void closeSocket(int& fd, char* localPath) noexcept;
    Status request(int fd, const char* cmd, char* reply, size_t cap, size_t& len, int timeoutMs = 2000) noexcept;
    Status command(const char* cmd) noexcept;  // expects "OK"
    Status setNetwork(int id, const char* name, const char* value, bool secret = false) noexcept;
    void enrichFromBss(ApInfo& ap) noexcept;

    char ctrlPath_[kPathLen]{};
    char cmdLocal_[kPathLen]{};
    char monLocal_[kPathLen]{};
    int cmdFd_ = -1;
    int monFd_ = -1;
    int networkId_ = -1;
    char reply_[kReplyLen]{};
    char bssReply_[kBssReplyLen]{};
};

}  // namespace wifi6
