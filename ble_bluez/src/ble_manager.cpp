// SPDX-License-Identifier: MIT
#include "ble/ble_manager.hpp"

#include <algorithm>

#include "ble/dbus.hpp"
#include "ble/utils.hpp"

namespace ble {

bool GattCharacteristicInfo::hasFlag(const std::string& f) const {
    return std::find(flags.begin(), flags.end(), f) != flags.end();
}

BleManager::BleManager(const std::string& adapterName) {
    GError* err = nullptr;
    conn_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    throwIfError(err, "connecting to the system bus");
    loop_ = g_main_loop_new(nullptr, FALSE);

    // Subscribe before the initial snapshot so nothing is missed in between.
    subscriptions_.push_back(g_dbus_connection_signal_subscribe(
        conn_, iface::kBluezService, iface::kObjectManager, "InterfacesAdded", nullptr, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onInterfacesAdded, this, nullptr));
    subscriptions_.push_back(g_dbus_connection_signal_subscribe(
        conn_, iface::kBluezService, iface::kObjectManager, "InterfacesRemoved", nullptr, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onInterfacesRemoved, this, nullptr));
    subscriptions_.push_back(g_dbus_connection_signal_subscribe(
        conn_, iface::kBluezService, iface::kProperties, "PropertiesChanged", nullptr, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onPropertiesChanged, this, nullptr));

    // Find the adapter.
    VariantPtr objects = callSync(conn_, "/", iface::kObjectManager, "GetManagedObjects", nullptr,
                                  G_VARIANT_TYPE("(a{oa{sa{sv}}})"));
    std::string adapterPath;
    GVariantIter* it = nullptr;
    const gchar* path;
    GVariant* ifaces;
    g_variant_get(objects.get(), "(a{oa{sa{sv}}})", &it);
    while (g_variant_iter_next(it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        VariantPtr guard(ifaces);
        VariantPtr adapterProps(g_variant_lookup_value(ifaces, iface::kAdapter, nullptr));
        if (!adapterProps) continue;
        std::string p = path;
        bool match = adapterName.empty() ||
                     (p.size() >= adapterName.size() &&
                      p.compare(p.size() - adapterName.size(), adapterName.size(), adapterName) == 0);
        if (match && adapterPath.empty()) adapterPath = p;
    }
    g_variant_iter_free(it);
    if (adapterPath.empty())
        throw std::runtime_error("Bluetooth adapter '" + adapterName + "' not found");
    adapter_ = std::make_unique<Adapter>(conn_, adapterPath);

    loadManagedObjects();
}

BleManager::~BleManager() {
    for (guint id : subscriptions_) g_dbus_connection_signal_unsubscribe(conn_, id);
    if (loop_) g_main_loop_unref(loop_);
    if (conn_) g_object_unref(conn_);
}

void BleManager::loadManagedObjects() {
    VariantPtr objects = callSync(conn_, "/", iface::kObjectManager, "GetManagedObjects", nullptr,
                                  G_VARIANT_TYPE("(a{oa{sa{sv}}})"));
    GVariantIter* it = nullptr;
    const gchar* path;
    GVariant* ifaces;
    g_variant_get(objects.get(), "(a{oa{sa{sv}}})", &it);
    while (g_variant_iter_next(it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        addInterfaces(path, ifaces);
        g_variant_unref(ifaces);
    }
    g_variant_iter_free(it);
}

void BleManager::applyDeviceProps(DeviceInfo& d, GVariant* props) {
    for (auto& [key, value] : toPropertyMap(props)) {
        GVariant* v = value.get();
        if (key == "Address") d.address = variantString(v);
        else if (key == "AddressType") d.addressType = variantString(v);
        else if (key == "Name") d.name = variantString(v);
        else if (key == "Alias") d.alias = variantString(v);
        else if (key == "RSSI") d.rssi = g_variant_get_int16(v);
        else if (key == "TxPower") d.txPower = g_variant_get_int16(v);
        else if (key == "Appearance") d.appearance = g_variant_get_uint16(v);
        else if (key == "Paired") d.paired = variantBool(v);
        else if (key == "Bonded") d.bonded = variantBool(v);
        else if (key == "Trusted") d.trusted = variantBool(v);
        else if (key == "Blocked") d.blocked = variantBool(v);
        else if (key == "Connected") d.connected = variantBool(v);
        else if (key == "ServicesResolved") d.servicesResolved = variantBool(v);
        else if (key == "UUIDs") d.uuids = variantStringList(v);
        else if (key == "ManufacturerData") {
            d.manufacturerData.clear();
            GVariantIter mit;
            guint16 id;
            GVariant* data;
            g_variant_iter_init(&mit, v);
            while (g_variant_iter_next(&mit, "{qv}", &id, &data)) {
                d.manufacturerData[id] = variantBytes(data);
                g_variant_unref(data);
            }
        }
    }
}

void BleManager::addInterfaces(const std::string& path, GVariant* ifaces) {
    if (!adapter_ || path.rfind(adapter_->path() + "/", 0) != 0) return;  // other adapters

    if (GVariant* props = g_variant_lookup_value(ifaces, iface::kDevice, nullptr)) {
        VariantPtr guard(props);
        bool isNew = devices_.find(path) == devices_.end();
        DeviceInfo& d = devices_[path];
        d.path = path;
        applyDeviceProps(d, props);
        emitDevice(isNew ? DeviceEvent::Added : DeviceEvent::Changed, d);
    }
    if (GVariant* props = g_variant_lookup_value(ifaces, iface::kGattService, nullptr)) {
        VariantPtr guard(props);
        auto m = toPropertyMap(props);
        GattServiceInfo s;
        s.path = path;
        s.uuid = variantString(m["UUID"].get());
        s.devicePath = variantString(m["Device"].get());
        s.primary = variantBool(m["Primary"].get(), true);
        services_[path] = s;
    }
    if (GVariant* props = g_variant_lookup_value(ifaces, iface::kGattChrc, nullptr)) {
        VariantPtr guard(props);
        auto m = toPropertyMap(props);
        GattCharacteristicInfo c;
        c.path = path;
        c.uuid = variantString(m["UUID"].get());
        c.servicePath = variantString(m["Service"].get());
        c.flags = variantStringList(m["Flags"].get());
        c.notifying = variantBool(m["Notifying"].get());
        chars_[path] = c;
    }
}

void BleManager::removeInterfaces(const std::string& path, const std::vector<std::string>& ifaces) {
    for (const auto& i : ifaces) {
        if (i == iface::kDevice) {
            auto it = devices_.find(path);
            if (it != devices_.end()) {
                DeviceInfo d = it->second;
                devices_.erase(it);
                emitDevice(DeviceEvent::Removed, d);
            }
        } else if (i == iface::kGattService) {
            services_.erase(path);
        } else if (i == iface::kGattChrc) {
            chars_.erase(path);
        }
    }
}

void BleManager::propertiesChanged(const std::string& path, const std::string& ifaceName,
                                   GVariant* changed, const std::vector<std::string>& invalidated) {
    if (ifaceName == iface::kDevice) {
        auto it = devices_.find(path);
        if (it == devices_.end()) return;
        applyDeviceProps(it->second, changed);
        for (const auto& name : invalidated)
            if (name == "RSSI") it->second.rssi.reset();
        emitDevice(DeviceEvent::Changed, it->second);
    } else if (ifaceName == iface::kGattChrc) {
        auto it = chars_.find(path);
        if (it == chars_.end()) return;
        auto m = toPropertyMap(changed);
        if (m.count("Notifying")) it->second.notifying = variantBool(m["Notifying"].get());
        if (m.count("Value")) {
            auto bytes = variantBytes(m["Value"].get());
            auto listeners = valueListeners_;  // copy: callbacks may unsubscribe
            GattCharacteristicInfo info = it->second;
            for (auto& [id, cb] : listeners) cb(info, bytes);
        }
    }
}

void BleManager::emitDevice(DeviceEvent ev, const DeviceInfo& d) {
    auto listeners = deviceListeners_;  // copy: callbacks may unsubscribe
    for (auto& [id, cb] : listeners) cb(ev, d);
}

void BleManager::onInterfacesAdded(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                   const gchar*, GVariant* params, gpointer self) {
    const gchar* path;
    GVariant* ifaces;
    g_variant_get(params, "(&o@a{sa{sv}})", &path, &ifaces);
    static_cast<BleManager*>(self)->addInterfaces(path, ifaces);
    g_variant_unref(ifaces);
}

void BleManager::onInterfacesRemoved(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                     const gchar*, GVariant* params, gpointer self) {
    const gchar* path;
    GVariant* list;
    g_variant_get(params, "(&o@as)", &path, &list);
    auto ifaces = variantStringList(list);
    g_variant_unref(list);
    static_cast<BleManager*>(self)->removeInterfaces(path, ifaces);
}

void BleManager::onPropertiesChanged(GDBusConnection*, const gchar*, const gchar* path,
                                     const gchar*, const gchar*, GVariant* params, gpointer self) {
    const gchar* ifaceName;
    GVariant* changed;
    GVariant* invalidated;
    g_variant_get(params, "(&s@a{sv}@as)", &ifaceName, &changed, &invalidated);
    static_cast<BleManager*>(self)->propertiesChanged(path, ifaceName, changed,
                                                      variantStringList(invalidated));
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

int BleManager::subscribeDevices(DeviceCallback cb) {
    int id = nextListenerId_++;
    deviceListeners_[id] = std::move(cb);
    return id;
}

int BleManager::subscribeValues(ValueCallback cb) {
    int id = nextListenerId_++;
    valueListeners_[id] = std::move(cb);
    return id;
}

void BleManager::unsubscribe(int id) {
    deviceListeners_.erase(id);
    valueListeners_.erase(id);
}

std::vector<DeviceInfo> BleManager::devices() const {
    std::vector<DeviceInfo> out;
    for (const auto& [p, d] : devices_) out.push_back(d);
    return out;
}

std::optional<DeviceInfo> BleManager::device(const std::string& path) const {
    auto it = devices_.find(path);
    if (it == devices_.end()) return std::nullopt;
    return it->second;
}

std::optional<DeviceInfo> BleManager::findByAddress(const std::string& address) const {
    std::string want = normalizeAddress(address);
    for (const auto& [p, d] : devices_)
        if (d.address == want) return d;
    return std::nullopt;
}

std::vector<GattServiceInfo> BleManager::services(const std::string& devicePath) const {
    std::vector<GattServiceInfo> out;
    for (const auto& [p, s] : services_)
        if (s.devicePath == devicePath) out.push_back(s);
    return out;
}

std::vector<GattCharacteristicInfo> BleManager::characteristics(const std::string& servicePath) const {
    std::vector<GattCharacteristicInfo> out;
    for (const auto& [p, c] : chars_)
        if (c.servicePath == servicePath) out.push_back(c);
    return out;
}

std::optional<GattCharacteristicInfo> BleManager::findCharacteristic(const std::string& devicePath,
                                                                     const std::string& uuid) const {
    std::string want = normalizeUuid(uuid);
    for (const auto& [p, c] : chars_) {
        if (c.uuid != want) continue;
        auto s = services_.find(c.servicePath);
        if (s != services_.end() && s->second.devicePath == devicePath) return c;
    }
    return std::nullopt;
}

void BleManager::run() {
    // quit() may already have been called synchronously (e.g. the device was
    // already connected); g_main_loop_run() would otherwise ignore that.
    if (!quitRequested_) g_main_loop_run(loop_);
    quitRequested_ = false;
}

void BleManager::quit(int exitCode) {
    exitCode_ = exitCode;
    quitRequested_ = true;
    g_main_loop_quit(loop_);
}

}  // namespace ble
