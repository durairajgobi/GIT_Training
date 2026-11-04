// SPDX-License-Identifier: MIT
// BleManager: policy layer on top of an IBleBackend.
//
//  * scanning with host-side filtering, merged with background scanning for
//    managed devices that are not currently visible
//  * serialized connection establishment (one LE Create Connection at a time,
//    optional scan pause while connecting - many controllers need both)
//  * per-device connection state machine with timeouts, auto-reconnect with
//    exponential back-off and persistent notification subscriptions
//  * GATT client read / write / notify routing
//  * peripheral role: advertising + local GATT server
//
// Threading: every public method is thread-safe and asynchronous (work is
// posted to the scheduler). Callbacks and listener events are delivered on the
// scheduler thread. devices()/device()/adapterState()/scanning() return
// thread-safe snapshots.
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "blemgr/backend.hpp"
#include "blemgr/reconnect_policy.hpp"
#include "blemgr/scheduler.hpp"
#include "blemgr/types.hpp"

namespace blemgr {

struct ManagerConfig {
    bool powerOnStart = true;
    std::chrono::milliseconds connectTimeout{15000};  // Connecting -> Ready budget
    unsigned maxConnections = 4;
    bool pauseScanDuringConnect = true;
    std::chrono::milliseconds staleDeviceTimeout{120000};  // prune unmanaged, unseen devices
    std::chrono::milliseconds scanRetryDelay{2000};
    ReconnectConfig reconnect;
};

class IBleListener {
public:
    virtual ~IBleListener() = default;
    virtual void onAdapterStateChanged(AdapterState) {}
    virtual void onScanResult(const DeviceInfo&) {}
    virtual void onConnectionStateChanged(const std::string& /*addr*/, ConnState,
                                          const Status& /*reason*/) {}
    virtual void onServicesDiscovered(const std::string& /*addr*/,
                                      const std::vector<GattServiceInfo>&) {}
    virtual void onNotification(const std::string& /*addr*/, const CharRef&, const Bytes&) {}
    virtual void onPairingResult(const std::string& /*addr*/, const Status&) {}
};

struct DeviceSnapshot {
    DeviceInfo info;
    ConnState state = ConnState::Disconnected;
    bool managed = false;
    uint32_t reconnectAttempts = 0;
    std::vector<GattServiceInfo> services;
    std::vector<CharRef> subscriptions;
};

class BleManager : private IBackendEvents {
public:
    BleManager(IBleBackend& backend, IScheduler& scheduler, ManagerConfig cfg = {});
    ~BleManager() override;
    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;

    /// Register before start(); listeners must outlive the manager.
    void addListener(IBleListener* l);

    void start(StatusCallback done = {});
    /// Disconnects links, stops scan/advertising and the backend. Call before
    /// destroying the manager or stopping the scheduler.
    void stop(StatusCallback done = {});

    void setPowered(bool on, StatusCallback done = {});

    // ---- scanning
    void startScan(ScanFilter filter, std::chrono::milliseconds duration = {},
                   StatusCallback done = {});
    void stopScan(StatusCallback done = {});

    // ---- central role
    /// Queues a connection. With autoReconnect the link is maintained until disconnect().
    void connect(std::string address, ConnectOptions opts = {}, StatusCallback accepted = {});
    void disconnect(std::string address, StatusCallback done = {});
    /// Disconnects and removes the device (and its bond) from the stack.
    void forget(std::string address, StatusCallback done = {});
    void read(std::string address, CharRef ch, ReadCallback cb);
    void write(std::string address, CharRef ch, Bytes value,
               WriteType type = WriteType::WithResponse, StatusCallback done = {});
    /// Persistent: re-enabled automatically after every reconnect.
    void subscribe(std::string address, CharRef ch, StatusCallback done = {});
    void unsubscribe(std::string address, CharRef ch, StatusCallback done = {});

    // ---- peripheral role
    void startAdvertising(AdvertisementData adv, StatusCallback done = {});
    void stopAdvertising(StatusCallback done = {});
    void publishGattServer(std::vector<LocalService> services, StatusCallback done = {});
    void notifyLocal(CharRef ch, Bytes value, StatusCallback done = {});

    // ---- thread-safe snapshots
    std::vector<DeviceSnapshot> devices() const;
    std::optional<DeviceSnapshot> device(const std::string& address) const;
    AdapterState adapterState() const { return adapterPublic_.load(); }
    bool scanning() const { return scanningPublic_.load(); }

private:
    struct Record {
        explicit Record(const ReconnectConfig& rc, uint32_t seed) : policy(rc, seed) {}
        DeviceInfo info;
        ConnState state = ConnState::Disconnected;
        bool managed = false;
        ConnectOptions opts;
        std::set<CharRef> subscriptions;
        std::vector<GattServiceInfo> services;
        ReconnectPolicy policy;
        IScheduler::TimerId timer = 0;
        uint64_t gen = 0;  // invalidates callbacks of superseded attempts
        IScheduler::Clock::time_point lastSeen{};
    };

    // IBackendEvents
    void onAdapterChanged(AdapterState state) override;
    void onDeviceUpdated(const DeviceInfo& info) override;
    void onDeviceRemoved(const std::string& address) override;
    void onConnectionChanged(const std::string& address, bool connected) override;
    void onServicesResolved(const std::string& address,
                            const std::vector<GattServiceInfo>& services) override;
    void onNotification(const std::string& address, const CharRef& ch,
                        const Bytes& value) override;

    // loop-thread implementation
    void doStart(const StatusCallback& done);
    void doStop(const StatusCallback& done);
    void doConnect(const std::string& addr, const ConnectOptions& opts,
                   const StatusCallback& cb);
    void doDisconnect(const std::string& addr, const StatusCallback& cb);

    Record& record(const std::string& addr);
    Record* find(const std::string& addr);
    void setState(Record& r, ConnState s, const Status& reason = {});
    void publish(const Record& r);
    void cancelTimer(Record& r);

    void enqueue(Record& r);
    void pumpQueue();
    void beginAttempt(Record& r);
    void onConnectResult(const std::string& addr, uint64_t gen, const Status& st);
    void onAttemptTimeout(const std::string& addr, uint64_t gen);
    void attemptFailed(Record& r, const Status& st, bool waitForAdvert);
    void scheduleRetry(Record& r, const Status& reason);
    void enterDiscovering(Record& r);
    void becomeReady(Record& r);
    void linkLost(Record& r);
    void releaseSlot(const std::string& addr);
    unsigned activeLinks() const;

    bool needBackgroundScan() const;
    void reconcileScan();
    void schedulePrune();
    void prune();

    template <class F> void notify(F&& f) {
        for (auto* l : listeners_) f(*l);
    }

    IBleBackend& backend_;
    IScheduler& sched_;
    ManagerConfig cfg_;
    std::vector<IBleListener*> listeners_;

    // loop-thread state
    bool started_ = false;
    AdapterState adapter_ = AdapterState::Unavailable;
    std::map<std::string, Record> records_;
    std::deque<std::string> queue_;
    std::optional<std::string> inFlight_;
    uint32_t seed_ = 0x5eed;

    bool userScan_ = false;
    ScanFilter userFilter_;
    IScheduler::TimerId userScanTimer_ = 0;
    bool scanActive_ = false;
    bool scanOpPending_ = false;
    std::string activeFilterKey_;
    IScheduler::TimerId scanRetryTimer_ = 0;
    IScheduler::TimerId pruneTimer_ = 0;

    // cross-thread snapshots
    mutable std::mutex mu_;
    std::map<std::string, DeviceSnapshot> snapshot_;
    std::atomic<AdapterState> adapterPublic_{AdapterState::Unavailable};
    std::atomic<bool> scanningPublic_{false};
};

}  // namespace blemgr
