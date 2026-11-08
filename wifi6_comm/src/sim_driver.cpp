// SPDX-License-Identifier: MIT
#include "wifi6/sim_driver.hpp"

#include <cstdio>
#include <cstring>

namespace wifi6 {

namespace {
// Rough HE (802.11ax) MCS from RSSI, and the PHY rate for 2 spatial streams,
// 0.8 us GI, 80 MHz channel (Mbit/s).
uint8_t mcsForRssi(int16_t rssi) noexcept {
    if (rssi >= -50) return 11;
    if (rssi >= -55) return 9;
    if (rssi >= -60) return 7;
    if (rssi >= -65) return 5;
    if (rssi >= -70) return 3;
    if (rssi >= -75) return 1;
    return 0;
}
constexpr uint16_t kHe80Nss2Mbps[12] = {72, 144, 216, 288, 432, 576, 649, 721, 865, 961, 1081, 1201};
}  // namespace

bool SimDriver::addAp(const ApInfo& ap) noexcept {
    if (apCount_ >= kMaxAps) return false;
    aps_[apCount_++] = ap;
    return true;
}

ApInfo* SimDriver::find(const Bssid& b) noexcept {
    for (size_t i = 0; i < apCount_; ++i)
        if (aps_[i].bssid == b) return &aps_[i];
    return nullptr;
}

void SimDriver::setApRssi(const Bssid& b, int16_t rssi) noexcept {
    if (ApInfo* ap = find(b)) ap->rssiDbm = rssi;
}

void SimDriver::setPassphrase(const char* pass) noexcept {
    std::snprintf(pass_, sizeof pass_, "%s", pass ? pass : "");
}

void SimDriver::push(DriverEventType type, const Bssid& b, uint16_t reason) noexcept {
    DriverEvent ev;
    ev.type = type;
    ev.bssid = b;
    ev.reason = reason;
    if (const ApInfo* ap = find(b)) ev.rssiDbm = ap->rssiDbm;
    (void)events_.push(ev);
}

void SimDriver::dropLink(uint16_t reason) noexcept {
    if (!connected_) return;
    connected_ = false;
    twtActive_ = false;
    push(DriverEventType::Disconnected, cur_, reason);
}

void SimDriver::terminate() noexcept {
    connected_ = false;
    twtActive_ = false;
    push(DriverEventType::DriverTerminated);
}

Status SimDriver::open() noexcept {
    open_ = true;
    return Status::Ok;
}

void SimDriver::close() noexcept {
    open_ = false;
    connected_ = false;
    twtActive_ = false;
}

Status SimDriver::startScan() noexcept {
    if (!open_) return Status::NotConnected;
    ++scanCount_;
    push(scanFails_ ? DriverEventType::ScanFailed : DriverEventType::ScanDone);
    return Status::Ok;
}

Status SimDriver::scanResults(ApInfo* out, size_t cap, size_t& count) noexcept {
    count = 0;
    if (!open_) return Status::NotConnected;
    for (size_t i = 0; i < apCount_ && count < cap; ++i)
        if (aps_[i].rssiDbm >= -95) out[count++] = aps_[i];  // below sensitivity = invisible
    return Status::Ok;
}

Status SimDriver::connect(const NetworkConfig& cfg, const Bssid* pinned) noexcept {
    if (!open_) return Status::NotConnected;
    const Status v = cfg.validate();
    if (!ok(v)) return v;
    ++connectAttempts_;

    ApInfo* target = nullptr;
    if (pinned) {
        target = find(*pinned);
    } else {
        for (size_t i = 0; i < apCount_; ++i)
            if (std::strcmp(aps_[i].ssid, cfg.ssid) == 0 && (!target || aps_[i].rssiDbm > target->rssiDbm))
                target = &aps_[i];
    }
    if (!target || std::strcmp(target->ssid, cfg.ssid) != 0 || target->rssiDbm < -90) {
        push(DriverEventType::NetworkNotFound);
        return Status::Ok;
    }
    if (cfg.needsPassphrase() && std::strcmp(cfg.passphrase, pass_) != 0) {
        push(DriverEventType::AuthFailed, target->bssid);
        return Status::Ok;
    }
    connected_ = true;
    twtActive_ = false;
    cur_ = target->bssid;
    push(DriverEventType::Connected, cur_);
    return Status::Ok;
}

Status SimDriver::disconnect() noexcept {
    if (connected_) {
        connected_ = false;
        twtActive_ = false;
        push(DriverEventType::Disconnected, cur_, 3 /* deauth leaving */);
    }
    return Status::Ok;
}

Status SimDriver::roam(const Bssid& target) noexcept {
    if (!connected_) return Status::NotConnected;
    if (!find(target)) return Status::InvalidArg;
    ++roamCount_;
    cur_ = target;
    twtActive_ = false;  // TWT agreements do not survive reassociation
    push(DriverEventType::Connected, cur_);
    return Status::Ok;
}

Status SimDriver::pollEvent(DriverEvent& ev) noexcept {
    return events_.pop(ev) ? Status::Ok : Status::WouldBlock;
}

Status SimDriver::linkStats(LinkStats& out) noexcept {
    if (!connected_) return Status::NotConnected;
    const ApInfo* ap = find(cur_);
    if (!ap) return Status::NotConnected;
    LinkStats s;
    s.rssiDbm = ap->rssiDbm;
    s.noiseDbm = -95;
    s.freqMhz = ap->freqMhz;
    s.heMcs = mcsForRssi(ap->rssiDbm);
    s.nss = 2;
    uint32_t mbps = kHe80Nss2Mbps[s.heMcs];
    switch (ap->band()) {
        case Band::Ghz6: mbps *= 2; break;  // 160 MHz
        case Band::Ghz2_4: mbps /= 4; break;  // 20 MHz
        default: break;
    }
    s.txRateKbps = mbps * 1000u;
    out = s;
    return Status::Ok;
}

Status SimDriver::setupTwt(const TwtParams& p) noexcept {
    if (!connected_) return Status::NotConnected;
    if (!twtSupported_) return Status::NotSupported;
    twt_ = p;
    twtActive_ = true;
    return Status::Ok;
}

Status SimDriver::teardownTwt(uint8_t) noexcept {
    twtActive_ = false;
    return Status::Ok;
}

}  // namespace wifi6
