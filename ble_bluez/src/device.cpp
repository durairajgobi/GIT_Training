// SPDX-License-Identifier: MIT
#include "ble/device.hpp"

#include "ble/dbus.hpp"

namespace ble {

namespace {
CallCallback toCallCallback(DoneCallback cb) {
    return [cb = std::move(cb)](CallResult r) {
        if (!cb) return;
        cb(r.ok, r.ok ? std::string() : (r.errorName.empty() ? r.errorMessage
                                                             : r.errorName + ": " + r.errorMessage));
    };
}
}  // namespace

void Device::pair(DoneCallback cb, int timeoutMs) {
    callAsync(conn_, path_, iface::kDevice, "Pair", nullptr, timeoutMs,
              [cb = std::move(cb)](CallResult r) {
                  // Already paired is not an error for our purposes.
                  if (!r.ok && r.errorName == "org.bluez.Error.AlreadyExists") r.ok = true;
                  if (cb) cb(r.ok, r.ok ? "" : r.errorName + ": " + r.errorMessage);
              });
}

void Device::connect(DoneCallback cb, int timeoutMs) {
    callAsync(conn_, path_, iface::kDevice, "Connect", nullptr, timeoutMs,
              [cb = std::move(cb)](CallResult r) {
                  if (!r.ok && r.errorName == "org.bluez.Error.AlreadyConnected") r.ok = true;
                  if (cb) cb(r.ok, r.ok ? "" : r.errorName + ": " + r.errorMessage);
              });
}

void Device::disconnect(DoneCallback cb) {
    callAsync(conn_, path_, iface::kDevice, "Disconnect", nullptr, 10000,
              toCallCallback(std::move(cb)));
}

void Device::cancelPairing() {
    callAsync(conn_, path_, iface::kDevice, "CancelPairing", nullptr, 5000, nullptr);
}

void Device::setTrusted(bool trusted) {
    setProperty(conn_, path_, iface::kDevice, "Trusted", g_variant_new_boolean(trusted));
}

void Device::setBlocked(bool blocked) {
    setProperty(conn_, path_, iface::kDevice, "Blocked", g_variant_new_boolean(blocked));
}

// --- DeviceConnector --------------------------------------------------------

std::shared_ptr<DeviceConnector> DeviceConnector::create(BleManager& mgr, std::string devicePath,
                                                         Options opts, DoneCallback done) {
    return std::shared_ptr<DeviceConnector>(
        new DeviceConnector(mgr, std::move(devicePath), opts, std::move(done)));
}

DeviceConnector::DeviceConnector(BleManager& mgr, std::string devicePath, Options opts,
                                 DoneCallback done)
    : mgr_(mgr), dev_(mgr.connection(), std::move(devicePath)), opts_(opts), done_(std::move(done)) {}

DeviceConnector::~DeviceConnector() {
    if (listener_) mgr_.unsubscribe(listener_);
    if (timer_) g_source_remove(timer_);
}

void DeviceConnector::start() {
    auto info = mgr_.device(dev_.path());
    if (!info) return finish(false, "device " + dev_.path() + " is not known to BlueZ");
    if (opts_.pair && !info->paired) return stepPair();
    stepTrust();
}

void DeviceConnector::stepPair() {
    g_message("[%s] pairing...", dev_.path().c_str());
    auto self = shared_from_this();
    dev_.pair([self](bool ok, const std::string& err) {
        if (!ok) return self->finish(false, "pairing failed: " + err);
        g_message("[%s] paired", self->dev_.path().c_str());
        self->stepTrust();
    });
}

void DeviceConnector::stepTrust() {
    if (opts_.trust) {
        try {
            dev_.setTrusted(true);
        } catch (const std::exception& e) {
            return finish(false, std::string("setting Trusted failed: ") + e.what());
        }
    }
    stepConnect();
}

void DeviceConnector::stepConnect() {
    auto info = mgr_.device(dev_.path());
    if (info && info->connected) return stepWaitServices();

    ++attempt_;
    g_message("[%s] connecting (attempt %d/%d)...", dev_.path().c_str(), attempt_,
              opts_.connectAttempts);
    auto self = shared_from_this();
    dev_.connect([self](bool ok, const std::string& err) {
        if (self->finished_) return;
        if (ok) return self->stepWaitServices();
        if (self->attempt_ >= self->opts_.connectAttempts)
            return self->finish(false, "connect failed: " + err);
        g_message("[%s] connect failed (%s), retrying", self->dev_.path().c_str(), err.c_str());
        self->timer_ = addTimeout(self->opts_.retryDelayMs, [self] {
            self->timer_ = 0;
            self->stepConnect();
            return false;
        });
    });
}

void DeviceConnector::stepWaitServices() {
    auto info = mgr_.device(dev_.path());
    if (info && info->servicesResolved) return finish(true, "");

    // Hold only a weak reference from the listener/timer to avoid a cycle.
    std::weak_ptr<DeviceConnector> weak = shared_from_this();
    listener_ = mgr_.subscribeDevices([weak](DeviceEvent ev, const DeviceInfo& d) {
        auto self = weak.lock();
        if (!self || d.path != self->dev_.path()) return;
        if (ev == DeviceEvent::Removed) return self->finish(false, "device removed");
        if (d.servicesResolved) self->finish(true, "");
        else if (!d.connected) self->finish(false, "device disconnected during service discovery");
    });
    timer_ = addTimeout(opts_.servicesTimeoutMs, [weak] {
        if (auto self = weak.lock()) {
            self->timer_ = 0;
            self->finish(false, "timed out waiting for GATT service discovery");
        }
        return false;
    });
}

void DeviceConnector::finish(bool ok, const std::string& error) {
    if (finished_) return;
    finished_ = true;
    if (listener_) { mgr_.unsubscribe(listener_); listener_ = 0; }
    if (timer_) { g_source_remove(timer_); timer_ = 0; }
    auto done = std::move(done_);
    if (done) done(ok, error);
}

}  // namespace ble
