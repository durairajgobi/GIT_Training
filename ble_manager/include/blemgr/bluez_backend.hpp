// SPDX-License-Identifier: MIT
// IBleBackend implementation on BlueZ >= 5.50 via its D-Bus API (GIO/GDBus).
// The header is GLib-free; everything lives behind a pimpl.
#pragma once

#include <memory>
#include <string>

#include "blemgr/backend.hpp"

namespace blemgr {

struct BluezOptions {
    std::string adapter = "hci0";
    /// D-Bus address to use instead of the system bus (tests / containers).
    std::string busAddress;
    std::string busName = "org.bluez";
    /// Pairing agent IO capability; empty disables the built-in agent.
    /// One of: NoInputNoOutput, DisplayOnly, DisplayYesNo, KeyboardOnly, KeyboardDisplay.
    std::string agentCapability = "NoInputNoOutput";
    bool agentAutoAccept = true;   // confirm "just works" / numeric comparison
    uint32_t agentPasskey = 0;     // answered to RequestPasskey
    std::string agentPinCode = "0000";
    std::string objectRoot = "/com/tcu/blemgr";
    int callTimeoutMs = 30000;
};

class BluezBackend final : public IBleBackend {
public:
    explicit BluezBackend(BluezOptions opts = {});
    ~BluezBackend() override;

    Status start(IBackendEvents* events) override;
    void stop() override;

    void setPowered(bool on, StatusCallback cb) override;
    void startDiscovery(const ScanFilter& filter, StatusCallback cb) override;
    void stopDiscovery(StatusCallback cb) override;

    void connect(const std::string& address, StatusCallback cb) override;
    void disconnect(const std::string& address, StatusCallback cb) override;
    void pair(const std::string& address, StatusCallback cb) override;
    void setTrusted(const std::string& address, bool trusted, StatusCallback cb) override;
    void removeDevice(const std::string& address, StatusCallback cb) override;
    void read(const std::string& address, const CharRef& ch, ReadCallback cb) override;
    void write(const std::string& address, const CharRef& ch, const Bytes& value, WriteType type,
               StatusCallback cb) override;
    void setNotify(const std::string& address, const CharRef& ch, bool enable,
                   StatusCallback cb) override;

    void startAdvertising(const AdvertisementData& adv, StatusCallback cb) override;
    void stopAdvertising(StatusCallback cb) override;
    void registerGattServer(const std::vector<LocalService>& services, StatusCallback cb) override;
    void unregisterGattServer(StatusCallback cb) override;
    Status notifyLocal(const CharRef& ch, const Bytes& value) override;

    struct Impl;

private:
    std::unique_ptr<Impl> d_;
};

}  // namespace blemgr
