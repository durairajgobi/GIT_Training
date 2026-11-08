// SPDX-License-Identifier: MIT
// org.bluez.Device1 operations and a connect state machine
// (pair -> trust -> connect -> wait for GATT services).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ble/ble_manager.hpp"
#include "ble/dbus.hpp"

namespace ble {

class Device {
public:
    Device(GDBusConnection* conn, std::string path) : conn_(conn), path_(std::move(path)) {}
    const std::string& path() const { return path_; }

    // Asynchronous: these may trigger Agent1 callbacks into this process.
    void pair(DoneCallback cb, int timeoutMs = 60000);
    void connect(DoneCallback cb, int timeoutMs = 30000);
    void disconnect(DoneCallback cb);
    void cancelPairing();

    void setTrusted(bool trusted);
    void setBlocked(bool blocked);

private:
    GDBusConnection* conn_;
    std::string path_;
};

// Drives one device to the "connected + services resolved" state.
class DeviceConnector : public std::enable_shared_from_this<DeviceConnector> {
public:
    struct Options {
        bool pair = true;            // pair (bond) if not already paired
        bool trust = true;           // mark trusted so it may reconnect / skip authorisation
        int connectAttempts = 3;     // LE connections fail transiently quite often
        unsigned retryDelayMs = 1500;
        unsigned servicesTimeoutMs = 20000;
    };

    // The caller must keep the returned pointer alive until `done` fires.
    static std::shared_ptr<DeviceConnector> create(BleManager& mgr, std::string devicePath,
                                                   Options opts, DoneCallback done);
    ~DeviceConnector();
    void start();

private:
    DeviceConnector(BleManager& mgr, std::string devicePath, Options opts, DoneCallback done);
    void stepPair();
    void stepTrust();
    void stepConnect();
    void stepWaitServices();
    void finish(bool ok, const std::string& error);

    BleManager& mgr_;
    Device dev_;
    Options opts_;
    DoneCallback done_;
    int attempt_ = 0;
    int listener_ = 0;
    guint timer_ = 0;
    bool finished_ = false;
};

}  // namespace ble
