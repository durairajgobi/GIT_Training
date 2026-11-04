// SPDX-License-Identifier: MIT
#include "blemgr/ble_manager.hpp"

#include <algorithm>

#include "blemgr/log.hpp"

namespace blemgr {

namespace {
void reply(const StatusCallback& cb, const Status& s) {
    if (cb) cb(s);
}
bool isLinkState(ConnState s) {
    return s == ConnState::Connecting || s == ConnState::Discovering || s == ConnState::Ready;
}
}  // namespace

BleManager::BleManager(IBleBackend& backend, IScheduler& scheduler, ManagerConfig cfg)
    : backend_(backend), sched_(scheduler), cfg_(std::move(cfg)) {
    if (cfg_.maxConnections == 0) cfg_.maxConnections = 1;
}

BleManager::~BleManager() = default;

void BleManager::addListener(IBleListener* l) {
    if (l) listeners_.push_back(l);
}

// ======================================================= public (posting) ==

void BleManager::start(StatusCallback done) {
    sched_.post([this, done] { doStart(done); });
}

void BleManager::stop(StatusCallback done) {
    sched_.post([this, done] { doStop(done); });
}

void BleManager::setPowered(bool on, StatusCallback done) {
    sched_.post([this, on, done] {
        if (!started_) return reply(done, {Error::NotReady, "manager not started"});
        backend_.setPowered(on, done);
    });
}

void BleManager::startScan(ScanFilter filter, std::chrono::milliseconds duration,
                           StatusCallback done) {
    sched_.post([this, filter = std::move(filter), duration, done]() mutable {
        if (!started_) return reply(done, {Error::NotReady, "manager not started"});
        for (auto& u : filter.serviceUuids) {
            auto n = normalizeUuid(u);
            if (!n) return reply(done, {Error::InvalidArgument, "bad uuid " + u});
            u = *n;
        }
        for (auto& a : filter.addresses) {
            auto n = normalizeAddress(a);
            if (!n) return reply(done, {Error::InvalidArgument, "bad address " + a});
            a = *n;
        }
        userScan_ = true;
        userFilter_ = std::move(filter);
        sched_.cancel(userScanTimer_);
        userScanTimer_ = 0;
        if (duration.count() > 0) {
            userScanTimer_ = sched_.postDelayed(duration, [this] {
                userScanTimer_ = 0;
                userScan_ = false;
                BLE_LOGI("scan window elapsed");
                reconcileScan();
            });
        }
        // Report current matches immediately (devices cached by the stack).
        for (auto& kv : records_) {
            const DeviceInfo& info = kv.second.info;
            if (info.rssi && userFilter_.matches(info))
                notify([&](IBleListener& l) { l.onScanResult(info); });
        }
        reconcileScan();
        reply(done, {});
    });
}

void BleManager::stopScan(StatusCallback done) {
    sched_.post([this, done] {
        userScan_ = false;
        sched_.cancel(userScanTimer_);
        userScanTimer_ = 0;
        reconcileScan();
        reply(done, {});
    });
}

void BleManager::connect(std::string address, ConnectOptions opts, StatusCallback accepted) {
    sched_.post([this, address = std::move(address), opts = std::move(opts), accepted] {
        doConnect(address, opts, accepted);
    });
}

void BleManager::disconnect(std::string address, StatusCallback done) {
    sched_.post([this, address = std::move(address), done] { doDisconnect(address, done); });
}

void BleManager::forget(std::string address, StatusCallback done) {
    sched_.post([this, address = std::move(address), done] {
        auto a = normalizeAddress(address);
        if (!a) return reply(done, {Error::InvalidArgument, "bad address"});
        doDisconnect(*a, {});
        backend_.removeDevice(*a, [this, addr = *a, done](const Status& s) {
            if (Record* r = find(addr)) r->subscriptions.clear();
            reply(done, s);
        });
    });
}

void BleManager::read(std::string address, CharRef ch, ReadCallback cb) {
    sched_.post([this, address = std::move(address), ch = std::move(ch), cb] {
        auto a = normalizeAddress(address);
        auto c = ch.normalized();
        if (!a || !c) return cb({Error::InvalidArgument, "bad address or uuid"}, {});
        Record* r = find(*a);
        if (!r || r->state != ConnState::Ready) return cb({Error::NotConnected, *a}, {});
        backend_.read(*a, *c, cb);
    });
}

void BleManager::write(std::string address, CharRef ch, Bytes value, WriteType type,
                       StatusCallback done) {
    sched_.post([this, address = std::move(address), ch = std::move(ch),
                 value = std::move(value), type, done] {
        auto a = normalizeAddress(address);
        auto c = ch.normalized();
        if (!a || !c) return reply(done, {Error::InvalidArgument, "bad address or uuid"});
        Record* r = find(*a);
        if (!r || r->state != ConnState::Ready) return reply(done, {Error::NotConnected, *a});
        backend_.write(*a, *c, value, type, done ? done : [](const Status&) {});
    });
}

void BleManager::subscribe(std::string address, CharRef ch, StatusCallback done) {
    sched_.post([this, address = std::move(address), ch = std::move(ch), done] {
        auto a = normalizeAddress(address);
        auto c = ch.normalized();
        if (!a || !c) return reply(done, {Error::InvalidArgument, "bad address or uuid"});
        Record& r = record(*a);
        r.subscriptions.insert(*c);
        publish(r);
        if (r.state == ConnState::Ready)
            backend_.setNotify(*a, *c, true, done ? done : [](const Status&) {});
        else
            reply(done, {});  // will be enabled on the next Ready
    });
}

void BleManager::unsubscribe(std::string address, CharRef ch, StatusCallback done) {
    sched_.post([this, address = std::move(address), ch = std::move(ch), done] {
        auto a = normalizeAddress(address);
        auto c = ch.normalized();
        if (!a || !c) return reply(done, {Error::InvalidArgument, "bad address or uuid"});
        Record* r = find(*a);
        if (!r || !r->subscriptions.erase(*c)) return reply(done, {Error::NotFound, "not subscribed"});
        publish(*r);
        if (r->state == ConnState::Ready)
            backend_.setNotify(*a, *c, false, done ? done : [](const Status&) {});
        else
            reply(done, {});
    });
}

void BleManager::startAdvertising(AdvertisementData adv, StatusCallback done) {
    sched_.post([this, adv = std::move(adv), done]() mutable {
        if (!started_) return reply(done, {Error::NotReady, "manager not started"});
        for (auto& u : adv.serviceUuids) {
            auto n = normalizeUuid(u);
            if (!n) return reply(done, {Error::InvalidArgument, "bad uuid " + u});
            u = *n;
        }
        backend_.startAdvertising(adv, done ? done : [](const Status&) {});
    });
}

void BleManager::stopAdvertising(StatusCallback done) {
    sched_.post([this, done] { backend_.stopAdvertising(done ? done : [](const Status&) {}); });
}

void BleManager::publishGattServer(std::vector<LocalService> services, StatusCallback done) {
    sched_.post([this, services = std::move(services), done]() mutable {
        if (!started_) return reply(done, {Error::NotReady, "manager not started"});
        for (auto& s : services) {
            auto su = normalizeUuid(s.uuid);
            if (!su) return reply(done, {Error::InvalidArgument, "bad uuid " + s.uuid});
            s.uuid = *su;
            for (auto& c : s.characteristics) {
                auto cu = normalizeUuid(c.uuid);
                if (!cu) return reply(done, {Error::InvalidArgument, "bad uuid " + c.uuid});
                c.uuid = *cu;
            }
        }
        backend_.registerGattServer(services, done ? done : [](const Status&) {});
    });
}

void BleManager::notifyLocal(CharRef ch, Bytes value, StatusCallback done) {
    sched_.post([this, ch = std::move(ch), value = std::move(value), done] {
        auto c = ch.normalized();
        if (!c) return reply(done, {Error::InvalidArgument, "bad uuid"});
        reply(done, backend_.notifyLocal(*c, value));
    });
}

std::vector<DeviceSnapshot> BleManager::devices() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<DeviceSnapshot> v;
    v.reserve(snapshot_.size());
    for (const auto& [a, s] : snapshot_) v.push_back(s);
    return v;
}

std::optional<DeviceSnapshot> BleManager::device(const std::string& address) const {
    auto a = normalizeAddress(address);
    if (!a) return std::nullopt;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = snapshot_.find(*a);
    if (it == snapshot_.end()) return std::nullopt;
    return it->second;
}

// ============================================================ lifecycle ==

void BleManager::doStart(const StatusCallback& done) {
    if (started_) return reply(done, {});
    Status s = backend_.start(this);
    if (!s.ok()) {
        BLE_LOGE("backend start failed: %s", s.toString().c_str());
        return reply(done, s);
    }
    started_ = true;
    if (cfg_.powerOnStart && adapter_ == AdapterState::Off) {
        backend_.setPowered(true, [](const Status& st) {
            if (!st.ok()) BLE_LOGW("power on failed: %s", st.toString().c_str());
        });
    }
    schedulePrune();
    reconcileScan();
    pumpQueue();
    reply(done, {});
}

void BleManager::doStop(const StatusCallback& done) {
    if (!started_) return reply(done, {});
    sched_.cancel(userScanTimer_);
    sched_.cancel(scanRetryTimer_);
    sched_.cancel(pruneTimer_);
    userScanTimer_ = scanRetryTimer_ = pruneTimer_ = 0;
    userScan_ = false;
    queue_.clear();
    inFlight_.reset();

    for (auto& [addr, r] : records_) {
        cancelTimer(r);
        ++r.gen;
        r.managed = false;
        if (isLinkState(r.state)) backend_.disconnect(addr, [](const Status&) {});
        if (r.state != ConnState::Disconnected) setState(r, ConnState::Disconnected, {Error::Cancelled, "stopping"});
    }
    if (scanActive_) backend_.stopDiscovery([](const Status&) {});
    backend_.stopAdvertising([](const Status&) {});
    backend_.unregisterGattServer([](const Status&) {});
    backend_.stop();

    scanActive_ = scanOpPending_ = false;
    scanningPublic_ = false;
    started_ = false;
    reply(done, {});
}

// ========================================================= central role ==

void BleManager::doConnect(const std::string& address, const ConnectOptions& opts,
                           const StatusCallback& cb) {
    auto a = normalizeAddress(address);
    if (!a) return reply(cb, {Error::InvalidArgument, "bad address " + address});
    ConnectOptions o = opts;
    for (auto& s : o.subscriptions) {
        auto n = s.normalized();
        if (!n) return reply(cb, {Error::InvalidArgument, "bad uuid " + s.toString()});
        s = *n;
    }
    if (!started_) return reply(cb, {Error::NotReady, "manager not started"});

    Record& r = record(*a);
    r.managed = o.autoReconnect;
    r.opts = o;
    for (const auto& s : o.subscriptions) r.subscriptions.insert(s);
    r.policy.reset();
    publish(r);

    switch (r.state) {
    case ConnState::Queued:
    case ConnState::Connecting:
    case ConnState::Discovering:
    case ConnState::Ready:
        return reply(cb, {});  // already on its way
    default:
        break;
    }
    cancelTimer(r);
    enqueue(r);
    pumpQueue();
    reply(cb, {});
}

void BleManager::doDisconnect(const std::string& address, const StatusCallback& cb) {
    auto a = normalizeAddress(address);
    if (!a) return reply(cb, {Error::InvalidArgument, "bad address " + address});
    Record* r = find(*a);
    if (!r) return reply(cb, {Error::NotFound, *a});

    r->managed = false;
    cancelTimer(*r);
    ++r->gen;
    queue_.erase(std::remove(queue_.begin(), queue_.end(), *a), queue_.end());
    releaseSlot(*a);

    if (isLinkState(r->state) || r->info.connected) {
        setState(*r, ConnState::Disconnecting, {Error::Cancelled, "user request"});
        backend_.disconnect(*a, [this, addr = *a, cb](const Status& s) {
            Record* rr = find(addr);
            if (rr && rr->state == ConnState::Disconnecting && !rr->info.connected)
                setState(*rr, ConnState::Disconnected, {Error::Cancelled, "user request"});
            reply(cb, s);
        });
    } else {
        setState(*r, ConnState::Disconnected, {Error::Cancelled, "user request"});
        reply(cb, {});
    }
    reconcileScan();
    pumpQueue();
}

void BleManager::enqueue(Record& r) {
    const auto& a = r.info.address;
    if (std::find(queue_.begin(), queue_.end(), a) == queue_.end()) queue_.push_back(a);
    setState(r, ConnState::Queued);
}

unsigned BleManager::activeLinks() const {
    unsigned n = 0;
    for (const auto& [a, r] : records_)
        if (isLinkState(r.state) || r.state == ConnState::Disconnecting) ++n;
    return n;
}

void BleManager::pumpQueue() {
    if (!started_ || adapter_ != AdapterState::On || inFlight_) return;
    while (!queue_.empty()) {
        if (activeLinks() >= cfg_.maxConnections) {
            BLE_LOGD("connection limit (%u) reached, %zu queued", cfg_.maxConnections, queue_.size());
            return;
        }
        std::string addr = queue_.front();
        queue_.pop_front();
        Record* r = find(addr);
        if (!r || r->state != ConnState::Queued) continue;
        beginAttempt(*r);
        return;
    }
}

void BleManager::beginAttempt(Record& r) {
    const std::string addr = r.info.address;
    inFlight_ = addr;
    const uint64_t gen = ++r.gen;
    setState(r, ConnState::Connecting);
    r.timer = sched_.postDelayed(cfg_.connectTimeout,
                                 [this, addr, gen] { onAttemptTimeout(addr, gen); });
    reconcileScan();  // may pause discovery while the controller initiates
    backend_.connect(addr, [this, addr, gen](const Status& s) { onConnectResult(addr, gen, s); });
}

void BleManager::onConnectResult(const std::string& addr, uint64_t gen, const Status& st) {
    Record* r = find(addr);
    if (!r || r->gen != gen) return;  // superseded (timeout, user disconnect...)
    if (st.ok()) {
        if (r->state == ConnState::Connecting) enterDiscovering(*r);
        return;
    }
    if (r->state != ConnState::Connecting) return;
    BLE_LOGW("%s: connect failed: %s", addr.c_str(), st.toString().c_str());
    attemptFailed(*r, st, st.code() == Error::NotFound);
}

void BleManager::onAttemptTimeout(const std::string& addr, uint64_t gen) {
    Record* r = find(addr);
    if (!r || r->gen != gen) return;
    r->timer = 0;
    if (r->state != ConnState::Connecting && r->state != ConnState::Discovering) return;
    BLE_LOGW("%s: %s timed out", addr.c_str(), toString(r->state));
    // Cancels a pending LE Create Connection or tears down a half-discovered link.
    backend_.disconnect(addr, [](const Status&) {});
    attemptFailed(*r, {Error::Timeout, std::string(toString(r->state)) + " timed out"}, false);
}

void BleManager::attemptFailed(Record& r, const Status& st, bool waitForAdvert) {
    cancelTimer(r);
    ++r.gen;
    r.services.clear();
    releaseSlot(r.info.address);
    if (r.managed) {
        if (waitForAdvert) {
            setState(r, ConnState::WaitingAdvert, st);
        } else {
            scheduleRetry(r, st);
        }
    } else {
        setState(r, ConnState::Disconnected, st);
    }
    reconcileScan();
    pumpQueue();
}

void BleManager::scheduleRetry(Record& r, const Status& reason) {
    auto delay = r.policy.next();
    if (!delay) {
        BLE_LOGW("%s: giving up after %u attempts", r.info.address.c_str(), r.policy.attempts());
        r.managed = false;
        setState(r, ConnState::Disconnected, {Error::Failed, "retry budget exhausted"});
        return;
    }
    BLE_LOGI("%s: retry #%u in %lld ms", r.info.address.c_str(), r.policy.attempts(),
             static_cast<long long>(delay->count()));
    const std::string addr = r.info.address;
    const uint64_t gen = r.gen;
    setState(r, ConnState::WaitingRetry, reason);
    r.timer = sched_.postDelayed(*delay, [this, addr, gen] {
        Record* rr = find(addr);
        if (!rr || rr->gen != gen) return;
        rr->timer = 0;
        if (rr->state != ConnState::WaitingRetry) return;
        enqueue(*rr);
        pumpQueue();
    });
}

void BleManager::enterDiscovering(Record& r) {
    releaseSlot(r.info.address);
    setState(r, ConnState::Discovering);
    if (r.info.servicesResolved && !r.services.empty()) {
        becomeReady(r);
    }
    reconcileScan();
    pumpQueue();
}

void BleManager::becomeReady(Record& r) {
    cancelTimer(r);
    r.policy.reset();
    setState(r, ConnState::Ready);
    const std::string addr = r.info.address;
    auto services = r.services;
    notify([&](IBleListener& l) { l.onServicesDiscovered(addr, services); });

    for (const auto& ch : r.subscriptions) {
        backend_.setNotify(addr, ch, true, [addr, ch](const Status& s) {
            if (!s.ok())
                BLE_LOGW("%s: enable notify %s failed: %s", addr.c_str(), ch.toString().c_str(),
                         s.toString().c_str());
        });
    }
    if (r.opts.pair && !r.info.paired) {
        backend_.pair(addr, [this, addr](const Status& s) {
            if (s.ok()) {
                // Trusted lets BlueZ accept the bonded device's reconnections.
                backend_.setTrusted(addr, true, [](const Status&) {});
            }
            notify([&](IBleListener& l) { l.onPairingResult(addr, s); });
        });
    }
}

void BleManager::linkLost(Record& r) {
    cancelTimer(r);
    ++r.gen;
    r.services.clear();
    if (r.managed) {
        scheduleRetry(r, {Error::NotConnected, "link lost"});
    } else {
        setState(r, ConnState::Disconnected, {Error::NotConnected, "link lost"});
    }
    reconcileScan();
    pumpQueue();
}

void BleManager::releaseSlot(const std::string& addr) {
    if (inFlight_ && *inFlight_ == addr) inFlight_.reset();
}

// ======================================================= backend events ==

void BleManager::onAdapterChanged(AdapterState state) {
    if (state == adapter_) return;
    BLE_LOGI("adapter %s -> %s", toString(adapter_), toString(state));
    adapter_ = state;
    adapterPublic_ = state;
    if (state != AdapterState::On) {
        // Controller gone or powered off: discovery is implicitly stopped.
        scanActive_ = false;
        scanningPublic_ = false;
        activeFilterKey_.clear();
    }
    notify([&](IBleListener& l) { l.onAdapterStateChanged(state); });
    if (state == AdapterState::Off && started_ && cfg_.powerOnStart) {
        BLE_LOGI("adapter is off; powering on");
        backend_.setPowered(true, [](const Status&) {});
    }
    reconcileScan();
    pumpQueue();
}

void BleManager::onDeviceUpdated(const DeviceInfo& info) {
    Record& r = record(info.address);
    const bool wasConnected = r.info.connected;
    r.info = info;
    r.info.connected = wasConnected;  // connection state is driven by onConnectionChanged
    r.lastSeen = sched_.now();
    publish(r);

    if (userScan_ && info.rssi && userFilter_.matches(info))
        notify([&](IBleListener& l) { l.onScanResult(r.info); });

    if (r.state == ConnState::WaitingAdvert && info.rssi) {
        BLE_LOGI("%s: advertising again (rssi %d), connecting", info.address.c_str(), *info.rssi);
        enqueue(r);
        reconcileScan();
        pumpQueue();
    }
}

void BleManager::onDeviceRemoved(const std::string& address) {
    Record* r = find(address);
    if (!r) return;
    r->info.rssi.reset();
    if (!r->managed && r->state == ConnState::Disconnected && r->subscriptions.empty()) {
        records_.erase(address);
        std::lock_guard<std::mutex> lk(mu_);
        snapshot_.erase(address);
        return;
    }
    publish(*r);
}

void BleManager::onConnectionChanged(const std::string& address, bool connected) {
    Record& r = record(address);
    r.info.connected = connected;
    if (!connected) r.info.servicesResolved = false;
    publish(r);

    if (connected) {
        switch (r.state) {
        case ConnState::Connecting:
            enterDiscovering(r);
            break;
        case ConnState::Discovering:
        case ConnState::Ready:
        case ConnState::Disconnecting:
            break;
        default: {
            // Link we did not initiate: incoming central, or the stack
            // reconnecting a bonded device on its own.
            queue_.erase(std::remove(queue_.begin(), queue_.end(), address), queue_.end());
            cancelTimer(r);
            const uint64_t gen = ++r.gen;
            if (r.managed) {
                r.timer = sched_.postDelayed(cfg_.connectTimeout,
                                             [this, address, gen] { onAttemptTimeout(address, gen); });
            }
            enterDiscovering(r);
            break;
        }
        }
        return;
    }

    switch (r.state) {
    case ConnState::Disconnecting:
        setState(r, ConnState::Disconnected, {Error::Cancelled, "user request"});
        reconcileScan();
        pumpQueue();
        break;
    case ConnState::Discovering:
    case ConnState::Ready:
        BLE_LOGW("%s: link lost in %s", address.c_str(), toString(r.state));
        linkLost(r);
        break;
    default:
        // Connecting: the pending Connect() call reports the failure.
        break;
    }
}

void BleManager::onServicesResolved(const std::string& address,
                                    const std::vector<GattServiceInfo>& services) {
    Record& r = record(address);
    r.services = services;
    r.info.servicesResolved = true;
    publish(r);
    if (r.state == ConnState::Connecting) enterDiscovering(r);  // property raced the reply
    if (r.state == ConnState::Discovering) becomeReady(r);
}

void BleManager::onNotification(const std::string& address, const CharRef& ch,
                                const Bytes& value) {
    notify([&](IBleListener& l) { l.onNotification(address, ch, value); });
}

// ============================================================= scanning ==

bool BleManager::needBackgroundScan() const {
    return std::any_of(records_.begin(), records_.end(), [](const auto& kv) {
        return kv.second.state == ConnState::WaitingAdvert;
    });
}

void BleManager::reconcileScan() {
    if (!started_ || scanOpPending_ || scanRetryTimer_) return;

    const bool background = needBackgroundScan();
    const bool paused = cfg_.pauseScanDuringConnect && inFlight_.has_value();
    const bool want = adapter_ == AdapterState::On && (userScan_ || background) && !paused;

    // Background scanning must see every device, so it drops the controller filter.
    ScanFilter f = background ? ScanFilter{} : userFilter_;
    const std::string key = f.key();

    auto onDone = [this](bool starting) {
        return [this, starting](const Status& s) {
            scanOpPending_ = false;
            if (starting) {
                scanActive_ = s.ok();
            } else {
                scanActive_ = false;  // treat a failed stop as stopped
            }
            scanningPublic_ = scanActive_;
            if (!s.ok()) {
                BLE_LOGW("%s discovery failed: %s", starting ? "start" : "stop",
                         s.toString().c_str());
                activeFilterKey_.clear();
                if (starting) {
                    scanRetryTimer_ = sched_.postDelayed(cfg_.scanRetryDelay, [this] {
                        scanRetryTimer_ = 0;
                        reconcileScan();
                    });
                    return;
                }
            }
            reconcileScan();
        };
    };

    if (want && !scanActive_) {
        BLE_LOGD("discovery start (%s)", background ? "background" : "user");
        scanOpPending_ = true;
        activeFilterKey_ = key;
        backend_.startDiscovery(f, onDone(true));
    } else if (scanActive_ && (!want || key != activeFilterKey_)) {
        BLE_LOGD("discovery stop");
        scanOpPending_ = true;
        backend_.stopDiscovery(onDone(false));
    }
}

void BleManager::schedulePrune() {
    auto period = std::max(cfg_.staleDeviceTimeout / 2, std::chrono::milliseconds(1000));
    pruneTimer_ = sched_.postDelayed(period, [this] {
        pruneTimer_ = 0;
        prune();
        schedulePrune();
    });
}

void BleManager::prune() {
    const auto now = sched_.now();
    for (auto it = records_.begin(); it != records_.end();) {
        Record& r = it->second;
        if (!r.managed && r.state == ConnState::Disconnected && r.subscriptions.empty() &&
            now - r.lastSeen > cfg_.staleDeviceTimeout) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                snapshot_.erase(it->first);
            }
            it = records_.erase(it);
        } else {
            if (r.info.rssi && now - r.lastSeen > cfg_.staleDeviceTimeout) {
                r.info.rssi.reset();  // out of range
                publish(r);
            }
            ++it;
        }
    }
}

// ============================================================== helpers ==

BleManager::Record& BleManager::record(const std::string& addr) {
    auto it = records_.find(addr);
    if (it == records_.end()) {
        it = records_.emplace(addr, Record(cfg_.reconnect, seed_++)).first;
        it->second.info.address = addr;
        it->second.lastSeen = sched_.now();
    }
    return it->second;
}

BleManager::Record* BleManager::find(const std::string& addr) {
    auto it = records_.find(addr);
    return it == records_.end() ? nullptr : &it->second;
}

void BleManager::setState(Record& r, ConnState s, const Status& reason) {
    if (r.state == s) return;
    BLE_LOGI("%s: %s -> %s%s%s", r.info.address.c_str(), toString(r.state), toString(s),
             reason.ok() ? "" : " | ", reason.ok() ? "" : reason.toString().c_str());
    r.state = s;
    publish(r);
    const std::string addr = r.info.address;
    notify([&](IBleListener& l) { l.onConnectionStateChanged(addr, s, reason); });
}

void BleManager::publish(const Record& r) {
    DeviceSnapshot s;
    s.info = r.info;
    s.state = r.state;
    s.managed = r.managed;
    s.reconnectAttempts = r.policy.attempts();
    s.services = r.services;
    s.subscriptions.assign(r.subscriptions.begin(), r.subscriptions.end());
    std::lock_guard<std::mutex> lk(mu_);
    snapshot_[r.info.address] = std::move(s);
}

void BleManager::cancelTimer(Record& r) {
    if (r.timer) sched_.cancel(r.timer);
    r.timer = 0;
}

}  // namespace blemgr
