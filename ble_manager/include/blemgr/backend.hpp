// SPDX-License-Identifier: MIT
// Hardware abstraction: the manager talks to the Bluetooth stack only through
// IBleBackend. The production implementation is BluezBackend (D-Bus); tests
// use an in-memory fake.
#pragma once

#include <string>
#include <vector>

#include "blemgr/types.hpp"

namespace blemgr {

/// Events raised by a backend. Always delivered on the loop thread.
class IBackendEvents {
public:
    virtual ~IBackendEvents() = default;
    virtual void onAdapterChanged(AdapterState state) = 0;
    /// Device appeared or one of its properties (RSSI, name, adv data...) changed.
    virtual void onDeviceUpdated(const DeviceInfo& info) = 0;
    virtual void onDeviceRemoved(const std::string& address) = 0;
    virtual void onConnectionChanged(const std::string& address, bool connected) = 0;
    virtual void onServicesResolved(const std::string& address,
                                    const std::vector<GattServiceInfo>& services) = 0;
    virtual void onNotification(const std::string& address, const CharRef& ch,
                                const Bytes& value) = 0;
};

/// All methods are invoked on the loop thread; completion callbacks are
/// delivered asynchronously on the loop thread (never re-entrantly).
class IBleBackend {
public:
    virtual ~IBleBackend() = default;

    virtual Status start(IBackendEvents* events) = 0;
    virtual void stop() = 0;

    // Adapter / discovery
    virtual void setPowered(bool on, StatusCallback cb) = 0;
    virtual void startDiscovery(const ScanFilter& filter, StatusCallback cb) = 0;
    virtual void stopDiscovery(StatusCallback cb) = 0;

    // Central role
    virtual void connect(const std::string& address, StatusCallback cb) = 0;
    virtual void disconnect(const std::string& address, StatusCallback cb) = 0;
    virtual void pair(const std::string& address, StatusCallback cb) = 0;
    virtual void setTrusted(const std::string& address, bool trusted, StatusCallback cb) = 0;
    virtual void removeDevice(const std::string& address, StatusCallback cb) = 0;
    virtual void read(const std::string& address, const CharRef& ch, ReadCallback cb) = 0;
    virtual void write(const std::string& address, const CharRef& ch, const Bytes& value,
                       WriteType type, StatusCallback cb) = 0;
    virtual void setNotify(const std::string& address, const CharRef& ch, bool enable,
                           StatusCallback cb) = 0;

    // Peripheral role
    virtual void startAdvertising(const AdvertisementData& adv, StatusCallback cb) = 0;
    virtual void stopAdvertising(StatusCallback cb) = 0;
    virtual void registerGattServer(const std::vector<LocalService>& services,
                                    StatusCallback cb) = 0;
    virtual void unregisterGattServer(StatusCallback cb) = 0;
    /// Updates a local characteristic value and notifies subscribed centrals.
    virtual Status notifyLocal(const CharRef& ch, const Bytes& value) = 0;
};

}  // namespace blemgr
