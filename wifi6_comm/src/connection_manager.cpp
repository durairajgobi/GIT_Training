// SPDX-License-Identifier: MIT
#include "wifi6/connection_manager.hpp"

#include <cstring>

#include "wifi6/log.hpp"

namespace wifi6 {

namespace {
constexpr const char* kTag = "conn";
constexpr int kMaxEventsPerTick = 16;
}  // namespace

const char* toString(ConnState s) noexcept {
    switch (s) {
        case ConnState::Idle: return "idle";
        case ConnState::Scanning: return "scanning";
        case ConnState::Connecting: return "connecting";
        case ConnState::Connected: return "connected";
        case ConnState::Backoff: return "backoff";
        case ConnState::Failed: return "failed";
    }
    return "?";
}

ConnectionManager::ConnectionManager(IWifiDriver& driver, const ConnectionPolicy& policy,
                                     IConnectionObserver* observer) noexcept
    : driver_(driver),
      policy_(policy),
      observer_(observer),
      selector_(policy.selector),
      backoff_(policy.backoffBaseMs, policy.backoffMaxMs, 0x5EED1234u),
      link_(policy.weakRssiDbm) {}

Status ConnectionManager::start(const NetworkConfig& cfg, uint64_t nowMs) noexcept {
    const Status v = cfg.validate();
    if (!ok(v)) return v;
    cfg_ = cfg;
    lastNow_ = nowMs;
    authFailures_ = 0;
    roams_ = 0;
    backoff_.reset();
    excludeValid_ = false;
    hasBgScanned_ = false;

    const Status s = driver_.open();
    if (!ok(s)) {
        // The supplicant may simply not be up yet (boot ordering): keep retrying.
        W6_LOGW(kTag, "driver open failed (%s), will retry", toString(s));
        needReopen_ = true;
        scheduleBackoff(nowMs, s);
        return Status::Ok;
    }
    needReopen_ = false;
    beginScan(nowMs, false);
    return Status::Ok;
}

void ConnectionManager::stop() noexcept {
    if (twtActive_) (void)driver_.teardownTwt(0);
    if (state_ == ConnState::Connected || state_ == ConnState::Connecting) (void)driver_.disconnect();
    twtActive_ = false;
    bgScanPending_ = false;
    roamPending_ = false;
    link_.reset();
    enter(ConnState::Idle, Status::Ok);
    secureZero(cfg_.passphrase, sizeof cfg_.passphrase);
}

void ConnectionManager::tick(uint64_t now) noexcept {
    lastNow_ = now;
    DriverEvent ev;
    for (int i = 0; i < kMaxEventsPerTick && ok(driver_.pollEvent(ev)); ++i) handleEvent(ev, now);

    switch (state_) {
        case ConnState::Scanning:
            if (now >= deadline_) {
                W6_LOGW(kTag, "scan timed out");
                scheduleBackoff(now, Status::Timeout);
            }
            break;
        case ConnState::Connecting:
            if (now >= deadline_) {
                W6_LOGW(kTag, "association timed out");
                exclude_ = target_.bssid;
                excludeValid_ = true;
                (void)driver_.disconnect();
                scheduleBackoff(now, Status::Timeout);
            }
            break;
        case ConnState::Backoff:
            if (now >= deadline_) {
                if (needReopen_) {
                    driver_.close();
                    const Status s = driver_.open();
                    if (!ok(s)) {
                        scheduleBackoff(now, s);
                        break;
                    }
                    needReopen_ = false;
                }
                beginScan(now, false);
            }
            break;
        case ConnState::Connected:
            if (bgScanPending_ && now - bgScanStartMs_ >= policy_.scanTimeoutMs) bgScanPending_ = false;
            if (roamPending_ && now >= roamDeadline_) {
                W6_LOGW(kTag, "roam did not complete");
                roamPending_ = false;
            }
            if (now >= nextStatsMs_) pollStats(now);
            break;
        case ConnState::Idle:
        case ConnState::Failed:
            break;
    }
}

void ConnectionManager::enter(ConnState next, Status reason) noexcept {
    if (!ok(reason)) lastError_ = reason;
    if (next == state_) return;
    const ConnState prev = state_;
    state_ = next;
    W6_LOGI(kTag, "%s -> %s (%s)", toString(prev), toString(next), toString(reason));
    if (observer_) observer_->onStateChanged(prev, next, reason);
}

void ConnectionManager::beginScan(uint64_t now, bool background) noexcept {
    const Status s = driver_.startScan();
    if (background) {
        lastBgScanMs_ = now;
        hasBgScanned_ = true;
        if (ok(s)) {
            bgScanPending_ = true;
            bgScanStartMs_ = now;
            W6_LOGD(kTag, "background scan (rssi %d dBm)", link_.smoothedRssi());
        }
        return;
    }
    if (!ok(s)) {
        scheduleBackoff(now, s);
        return;
    }
    deadline_ = now + policy_.scanTimeoutMs;
    enter(ConnState::Scanning, Status::Ok);
}

void ConnectionManager::scheduleBackoff(uint64_t now, Status reason) noexcept {
    const uint32_t delay = backoff_.next();
    deadline_ = now + delay;
    bgScanPending_ = false;
    roamPending_ = false;
    twtActive_ = false;
    link_.reset();
    W6_LOGI(kTag, "retry in %u ms (attempt %u, %s)", delay, backoff_.attempts(), toString(reason));
    enter(ConnState::Backoff, reason);
}

const ApInfo* ConnectionManager::findScanned(const Bssid& b) const noexcept {
    for (size_t i = 0; i < scanCount_; ++i)
        if (scan_[i].bssid == b) return &scan_[i];
    return nullptr;
}

void ConnectionManager::handleEvent(const DriverEvent& ev, uint64_t now) noexcept {
    switch (ev.type) {
        case DriverEventType::ScanDone:
            onScanDone(now);
            break;

        case DriverEventType::ScanFailed:
            bgScanPending_ = false;
            if (state_ == ConnState::Scanning) scheduleBackoff(now, Status::Busy);
            break;

        case DriverEventType::Connected:
            if (state_ == ConnState::Connecting || state_ == ConnState::Connected) onConnected(ev, now);
            break;

        case DriverEventType::Disconnected:
            if (state_ == ConnState::Connected) {
                W6_LOGW(kTag, "link lost (reason %u)", ev.reason);
                (void)driver_.disconnect();  // stop the supplicant's own reconnect logic
                scheduleBackoff(now, Status::NotConnected);
            } else if (state_ == ConnState::Connecting) {
                exclude_ = target_.bssid;
                excludeValid_ = true;
                (void)driver_.disconnect();
                scheduleBackoff(now, Status::NotConnected);
            }
            break;

        case DriverEventType::AuthFailed:
            if (state_ != ConnState::Connecting && state_ != ConnState::Backoff) break;
            ++authFailures_;
            W6_LOGW(kTag, "authentication failed (%u/%u)", authFailures_, policy_.maxAuthFailures);
            if (authFailures_ >= policy_.maxAuthFailures) {
                // Repeated failures mean bad credentials: stop rather than
                // hammer the AP (and risk being blacklisted by it).
                (void)driver_.disconnect();
                enter(ConnState::Failed, Status::AuthFailed);
            } else if (state_ == ConnState::Connecting) {
                (void)driver_.disconnect();
                scheduleBackoff(now, Status::AuthFailed);
            }
            break;

        case DriverEventType::ConnectFailed:
        case DriverEventType::NetworkNotFound:
            if (state_ == ConnState::Connecting) {
                exclude_ = target_.bssid;
                excludeValid_ = true;
                (void)driver_.disconnect();
                scheduleBackoff(now, ev.type == DriverEventType::NetworkNotFound ? Status::NoNetwork
                                                                                 : Status::NotConnected);
            }
            break;

        case DriverEventType::SignalChange:
            if (state_ == ConnState::Connected) nextStatsMs_ = now;  // re-evaluate promptly
            break;

        case DriverEventType::DriverTerminated:
            W6_LOGE(kTag, "driver terminated");
            needReopen_ = true;
            if (state_ != ConnState::Idle && state_ != ConnState::Failed) scheduleBackoff(now, Status::IoError);
            break;
    }
}

void ConnectionManager::onScanDone(uint64_t now) noexcept {
    size_t n = 0;
    const Status s = driver_.scanResults(scan_.data(), scan_.size(), n);
    scanCount_ = ok(s) ? n : 0;

    if (state_ == ConnState::Scanning) {
        if (!ok(s)) {
            scheduleBackoff(now, s);
            return;
        }
        const ApInfo* best = selector_.selectBest(scan_.data(), scanCount_, cfg_, excludeValid_ ? &exclude_ : nullptr);
        if (!best && excludeValid_) best = selector_.selectBest(scan_.data(), scanCount_, cfg_);
        excludeValid_ = false;
        if (!best) {
            W6_LOGW(kTag, "no eligible AP for '%s' among %zu BSSs", cfg_.ssid, scanCount_);
            scheduleBackoff(now, Status::NoNetwork);
            return;
        }
        target_ = *best;
        char b[18];
        best->bssid.format(b, sizeof b);
        W6_LOGI(kTag, "joining %s ch%u %s %d dBm %s%s", b, channelFromFreq(best->freqMhz), toString(best->band()),
                best->rssiDbm, toString(best->security), best->heCapable ? " HE" : "");
        const Status c = driver_.connect(cfg_, &best->bssid);
        if (!ok(c)) {
            scheduleBackoff(now, c);
            return;
        }
        deadline_ = now + policy_.connectTimeoutMs;
        enter(ConnState::Connecting, Status::Ok);
        return;
    }

    if (state_ == ConnState::Connected && bgScanPending_) {
        bgScanPending_ = false;
        if (!ok(s) || roamPending_) return;
        const ApInfo* t =
            selector_.selectRoamTarget(scan_.data(), scanCount_, cfg_, current_.bssid, link_.smoothedRssi());
        if (!t) return;
        char b[18];
        t->bssid.format(b, sizeof b);
        W6_LOGI(kTag, "roaming to %s %s %d dBm (current %d dBm)", b, toString(t->band()), t->rssiDbm,
                link_.smoothedRssi());
        if (ok(driver_.roam(t->bssid))) {
            roamTarget_ = *t;
            roamPending_ = true;
            roamDeadline_ = now + policy_.connectTimeoutMs;
        }
    }
}

void ConnectionManager::onConnected(const DriverEvent& ev, uint64_t now) noexcept {
    const bool wasConnected = state_ == ConnState::Connected;

    if (const ApInfo* ap = findScanned(ev.bssid))
        current_ = *ap;
    else if (roamPending_ && roamTarget_.bssid == ev.bssid)
        current_ = roamTarget_;
    else if (target_.bssid == ev.bssid)
        current_ = target_;
    else {
        current_ = ApInfo{};
        current_.bssid = ev.bssid;
        std::memcpy(current_.ssid, cfg_.ssid, sizeof current_.ssid);
    }

    authFailures_ = 0;
    excludeValid_ = false;
    backoff_.reset();
    link_.reset();
    nextStatsMs_ = now;
    twtActive_ = false;  // any previous agreement died with the old association

    if (wasConnected) {
        roamPending_ = false;
        ++roams_;
        if (observer_) observer_->onRoamed(current_);
    } else {
        enter(ConnState::Connected, Status::Ok);
    }
    setupTwt();
}

void ConnectionManager::pollStats(uint64_t now) noexcept {
    nextStatsMs_ = now + policy_.statsPollMs;
    LinkStats ls;
    if (!ok(driver_.linkStats(ls))) return;
    link_.update(ls);
    current_.rssiDbm = ls.rssiDbm;
    if (observer_) observer_->onLinkStats(ls);

    const bool scanDue = !hasBgScanned_ || now - lastBgScanMs_ >= policy_.bgScanMinIntervalMs;
    if (link_.isWeak() && !bgScanPending_ && !roamPending_ && scanDue) beginScan(now, true);
}

void ConnectionManager::setupTwt() noexcept {
    if (!policy_.enableTwt || policy_.twtIntervalUs == 0) return;
    if (!current_.twtResponder) {
        W6_LOGI(kTag, "AP does not advertise TWT responder support");
        return;
    }
    TwtParams p;
    Status s = TwtParams::fromMicros(policy_.twtIntervalUs, policy_.twtWakeUs, p);
    if (!ok(s)) {
        W6_LOGW(kTag, "invalid TWT parameters (%u us / %u us)", policy_.twtIntervalUs, policy_.twtWakeUs);
        return;
    }
    s = driver_.setupTwt(p);
    twtActive_ = ok(s);
    if (twtActive_)
        W6_LOGI(kTag, "TWT active: wake %u us every %llu us", p.wakeDurationUs(),
                static_cast<unsigned long long>(p.intervalUs()));
    else
        W6_LOGW(kTag, "TWT setup failed: %s", toString(s));
}

}  // namespace wifi6
