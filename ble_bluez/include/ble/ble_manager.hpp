// SPDX-License-Identifier: MIT
// Owns the system-bus connection, the GLib main loop and a live mirror of
// BlueZ's object tree (devices, GATT services and characteristics), kept in
// sync through ObjectManager.InterfacesAdded/Removed and PropertiesChanged.
#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ble/adapter.hpp"

namespace ble {

struct DeviceInfo {
    std::string path;
    std::string address;
    std::string addressType;   // "public" / "random"
    std::string name;          // advertised name (may be empty)
    std::string alias;         // name or address
    std::optional<int16_t> rssi;
    std::optional<int16_t> txPower;
    uint16_t appearance = 0;
    bool paired = false;
    bool bonded = false;
    bool trusted = false;
    bool blocked = false;
    bool connected = false;
    bool servicesResolved = false;
    std::vector<std::string> uuids;
    std::map<uint16_t, std::vector<uint8_t>> manufacturerData;

    std::string displayName() const { return name.empty() ? alias : name; }
};

struct GattServiceInfo {
    std::string path;
    std::string devicePath;
    std::string uuid;
    bool primary = true;
};

struct GattCharacteristicInfo {
    std::string path;
    std::string servicePath;
    std::string uuid;
    std::vector<std::string> flags;
    bool notifying = false;

    bool hasFlag(const std::string& f) const;
};

enum class DeviceEvent { Added, Changed, Removed };

class BleManager {
public:
    using DeviceCallback = std::function<void(DeviceEvent, const DeviceInfo&)>;
    using ValueCallback =
        std::function<void(const GattCharacteristicInfo&, const std::vector<uint8_t>&)>;

    // Connects to the system bus and locates the adapter (e.g. "hci0"; empty
    // = first adapter). Throws if bluetoothd or the adapter is missing.
    explicit BleManager(const std::string& adapterName = "hci0");
    ~BleManager();
    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;

    GDBusConnection* connection() const { return conn_; }
    Adapter& adapter() { return *adapter_; }

    // Listener registration; returns an id for unsubscribe().
    int subscribeDevices(DeviceCallback cb);
    int subscribeValues(ValueCallback cb);
    void unsubscribe(int id);

    std::vector<DeviceInfo> devices() const;
    std::optional<DeviceInfo> device(const std::string& path) const;
    std::optional<DeviceInfo> findByAddress(const std::string& address) const;

    std::vector<GattServiceInfo> services(const std::string& devicePath) const;
    std::vector<GattCharacteristicInfo> characteristics(const std::string& servicePath) const;
    std::optional<GattCharacteristicInfo> findCharacteristic(const std::string& devicePath,
                                                             const std::string& uuid) const;

    // Main loop control. quit() before run() makes run() return immediately.
    void run();
    void quit(int exitCode = 0);
    int exitCode() const { return exitCode_; }

private:
    void loadManagedObjects();
    void addInterfaces(const std::string& path, GVariant* ifaces);
    void removeInterfaces(const std::string& path, const std::vector<std::string>& ifaces);
    void propertiesChanged(const std::string& path, const std::string& iface, GVariant* changed,
                           const std::vector<std::string>& invalidated);
    void applyDeviceProps(DeviceInfo& d, GVariant* props);
    void emitDevice(DeviceEvent ev, const DeviceInfo& d);

    static void onInterfacesAdded(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                  const gchar*, GVariant*, gpointer);
    static void onInterfacesRemoved(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                    const gchar*, GVariant*, gpointer);
    static void onPropertiesChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                    const gchar*, GVariant*, gpointer);

    GDBusConnection* conn_ = nullptr;
    GMainLoop* loop_ = nullptr;
    std::unique_ptr<Adapter> adapter_;
    std::vector<guint> subscriptions_;

    std::map<std::string, DeviceInfo> devices_;
    std::map<std::string, GattServiceInfo> services_;
    std::map<std::string, GattCharacteristicInfo> chars_;

    int nextListenerId_ = 1;
    std::map<int, DeviceCallback> deviceListeners_;
    std::map<int, ValueCallback> valueListeners_;
    int exitCode_ = 0;
    bool quitRequested_ = false;
};

}  // namespace ble
