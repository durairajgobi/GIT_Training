// SPDX-License-Identifier: MIT
// Deterministic test doubles: virtual-time scheduler and scriptable backend.
#pragma once

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "blemgr/backend.hpp"
#include "blemgr/ble_manager.hpp"
#include "blemgr/scheduler.hpp"

namespace fake {

using namespace blemgr;
using namespace std::chrono_literals;

class Scheduler : public IScheduler {
public:
    void post(std::function<void()> fn) override { queue_.push_back(std::move(fn)); }
    TimerId postDelayed(std::chrono::milliseconds d, std::function<void()> fn) override {
        TimerId id = ++next_;
        timers_[id] = {now_ + d, std::move(fn)};
        return id;
    }
    void cancel(TimerId id) override { timers_.erase(id); }
    Clock::time_point now() const override { return now_; }

    /// Runs posted work until nothing is runnable at the current time.
    void runUntilIdle() {
        for (;;) {
            if (!queue_.empty()) {
                auto fn = std::move(queue_.front());
                queue_.pop_front();
                fn();
                continue;
            }
            auto due = nextDue();
            if (due == timers_.end() || due->second.at > now_) return;
            auto fn = std::move(due->second.fn);
            timers_.erase(due);
            fn();
        }
    }
    /// Advances virtual time, firing timers in order.
    void advance(std::chrono::milliseconds d) {
        const auto end = now_ + d;
        runUntilIdle();
        for (;;) {
            auto due = nextDue();
            if (due == timers_.end() || due->second.at > end) break;
            now_ = due->second.at;
            runUntilIdle();
        }
        now_ = end;
        runUntilIdle();
    }
    size_t pendingTimers() const { return timers_.size(); }

private:
    struct Timer {
        Clock::time_point at;
        std::function<void()> fn;
    };
    std::map<TimerId, Timer>::iterator nextDue() {
        auto best = timers_.end();
        for (auto it = timers_.begin(); it != timers_.end(); ++it)
            if (best == timers_.end() || it->second.at < best->second.at) best = it;
        return best;
    }
    Clock::time_point now_{};
    TimerId next_ = 0;
    std::deque<std::function<void()>> queue_;
    std::map<TimerId, Timer> timers_;
};

/// Backend whose operations complete only when the test says so, unless the
/// matching auto* flag is set. Callbacks are posted (never re-entrant).
class Backend : public IBleBackend {
public:
    explicit Backend(Scheduler& s) : sched(s) {}

    Scheduler& sched;
    IBackendEvents* ev = nullptr;
    bool known = true;  // device objects exist ("seen" by the stack)
    std::map<std::string, std::vector<GattServiceInfo>> services;

    // call log
    int startDiscoveryCalls = 0, stopDiscoveryCalls = 0;
    std::vector<std::string> connects, disconnects, pairs;
    std::vector<std::string> notifyOn, notifyOff;  // "addr|char"
    std::vector<std::pair<std::string, Bytes>> writes;
    bool discovering = false;
    ScanFilter lastFilter;
    std::map<std::string, StatusCallback> pendingConnect;
    bool autoConnect = false;  // complete connects immediately with success

    Status start(IBackendEvents* e) override {
        ev = e;
        ev->onAdapterChanged(AdapterState::On);
        return {};
    }
    void stop() override { ev = nullptr; }

    void setPowered(bool on, StatusCallback cb) override {
        later([this, on, cb] {
            if (ev) ev->onAdapterChanged(on ? AdapterState::On : AdapterState::Off);
            if (cb) cb({});
        });
    }
    void startDiscovery(const ScanFilter& f, StatusCallback cb) override {
        ++startDiscoveryCalls;
        lastFilter = f;
        later([this, cb] {
            discovering = true;
            cb({});
        });
    }
    void stopDiscovery(StatusCallback cb) override {
        ++stopDiscoveryCalls;
        later([this, cb] {
            discovering = false;
            cb({});
        });
    }
    void connect(const std::string& a, StatusCallback cb) override {
        connects.push_back(a);
        if (!known) return later([cb] { cb({Error::NotFound, "unknown"}); });
        if (autoConnect) return later([this, a, cb] { linkUp(a, cb); });
        pendingConnect[a] = cb;
    }
    void disconnect(const std::string& a, StatusCallback cb) override {
        disconnects.push_back(a);
        bool hadPending = pendingConnect.erase(a) > 0;
        later([this, a, cb, hadPending] {
            if (ev && !hadPending) ev->onConnectionChanged(a, false);
            cb({});
        });
    }
    void pair(const std::string& a, StatusCallback cb) override {
        pairs.push_back(a);
        later([cb] { cb({}); });
    }
    void setTrusted(const std::string&, bool, StatusCallback cb) override { later([cb] { cb({}); }); }
    void removeDevice(const std::string&, StatusCallback cb) override { later([cb] { cb({}); }); }
    void read(const std::string&, const CharRef&, ReadCallback cb) override {
        later([cb] { cb({}, Bytes{0x2a}); });
    }
    void write(const std::string& a, const CharRef&, const Bytes& v, WriteType,
               StatusCallback cb) override {
        writes.push_back({a, v});
        later([cb] { cb({}); });
    }
    void setNotify(const std::string& a, const CharRef& c, bool en, StatusCallback cb) override {
        (en ? notifyOn : notifyOff).push_back(a + "|" + c.characteristic);
        later([cb] { cb({}); });
    }
    void startAdvertising(const AdvertisementData&, StatusCallback cb) override { later([cb] { cb({}); }); }
    void stopAdvertising(StatusCallback cb) override { later([cb] { cb({}); }); }
    void registerGattServer(const std::vector<LocalService>&, StatusCallback cb) override {
        later([cb] { cb({}); });
    }
    void unregisterGattServer(StatusCallback cb) override { later([cb] { cb({}); }); }
    Status notifyLocal(const CharRef&, const Bytes&) override { return {}; }

    // ---- scripting helpers
    void completeConnect(const std::string& a, Status st = {}) {
        auto it = pendingConnect.find(a);
        if (it == pendingConnect.end()) return;
        auto cb = it->second;
        pendingConnect.erase(it);
        if (st.ok())
            linkUp(a, cb);
        else
            cb(st);
    }
    void linkUp(const std::string& a, const StatusCallback& cb) {
        if (ev) ev->onConnectionChanged(a, true);
        cb({});
        if (ev) ev->onServicesResolved(a, services[a]);
    }
    void dropLink(const std::string& a) { ev->onConnectionChanged(a, false); }
    void advertise(const std::string& a, int16_t rssi, const std::string& name = "",
                   std::vector<std::string> uuids = {}) {
        DeviceInfo d;
        d.address = a;
        d.rssi = rssi;
        d.name = name;
        d.serviceUuids = std::move(uuids);
        ev->onDeviceUpdated(d);
    }

private:
    void later(std::function<void()> fn) { sched.post(std::move(fn)); }
};

struct Listener : IBleListener {
    std::vector<std::pair<std::string, ConnState>> states;
    std::vector<std::string> scanResults;
    std::vector<std::pair<std::string, Bytes>> notifications;
    int servicesEvents = 0;
    int pairingEvents = 0;

    void onScanResult(const DeviceInfo& d) override { scanResults.push_back(d.address); }
    void onConnectionStateChanged(const std::string& a, ConnState s, const Status&) override {
        states.push_back({a, s});
    }
    void onServicesDiscovered(const std::string&, const std::vector<GattServiceInfo>&) override {
        ++servicesEvents;
    }
    void onNotification(const std::string& a, const CharRef&, const Bytes& v) override {
        notifications.push_back({a, v});
    }
    void onPairingResult(const std::string&, const Status&) override { ++pairingEvents; }

    ConnState last(const std::string& a) const {
        for (auto it = states.rbegin(); it != states.rend(); ++it)
            if (it->first == a) return it->second;
        return ConnState::Disconnected;
    }
};

}  // namespace fake
