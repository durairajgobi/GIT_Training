// SPDX-License-Identifier: MIT
#include "ble/adapter.hpp"

#include "ble/dbus.hpp"
#include "ble/utils.hpp"

namespace ble {

Adapter::Adapter(GDBusConnection* conn, std::string path) : conn_(conn), path_(std::move(path)) {}

std::string Adapter::address() const {
    return variantString(getProperty(conn_, path_, iface::kAdapter, "Address").get());
}
std::string Adapter::name() const {
    return variantString(getProperty(conn_, path_, iface::kAdapter, "Alias").get());
}
bool Adapter::powered() const {
    return variantBool(getProperty(conn_, path_, iface::kAdapter, "Powered").get());
}
bool Adapter::discovering() const {
    return variantBool(getProperty(conn_, path_, iface::kAdapter, "Discovering").get());
}

void Adapter::setPowered(bool on) {
    setProperty(conn_, path_, iface::kAdapter, "Powered", g_variant_new_boolean(on));
}

void Adapter::setDiscoverable(bool on, unsigned timeoutSec) {
    setProperty(conn_, path_, iface::kAdapter, "DiscoverableTimeout", g_variant_new_uint32(timeoutSec));
    setProperty(conn_, path_, iface::kAdapter, "Discoverable", g_variant_new_boolean(on));
}

void Adapter::setPairable(bool on, unsigned timeoutSec) {
    setProperty(conn_, path_, iface::kAdapter, "PairableTimeout", g_variant_new_uint32(timeoutSec));
    setProperty(conn_, path_, iface::kAdapter, "Pairable", g_variant_new_boolean(on));
}

void Adapter::setAlias(const std::string& alias) {
    setProperty(conn_, path_, iface::kAdapter, "Alias", g_variant_new_string(alias.c_str()));
}

void Adapter::setDiscoveryFilter(const DiscoveryFilter& f) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    const char* transport = f.transport == DiscoveryFilter::Transport::LE      ? "le"
                            : f.transport == DiscoveryFilter::Transport::BREDR ? "bredr"
                                                                               : "auto";
    g_variant_builder_add(&b, "{sv}", "Transport", g_variant_new_string(transport));
    if (f.rssi) g_variant_builder_add(&b, "{sv}", "RSSI", g_variant_new_int16(*f.rssi));
    if (!f.uuids.empty()) {
        std::vector<std::string> uuids;
        for (const auto& u : f.uuids) uuids.push_back(normalizeUuid(u));
        g_variant_builder_add(&b, "{sv}", "UUIDs", stringListVariant(uuids));
    }
    if (!f.pattern.empty())
        g_variant_builder_add(&b, "{sv}", "Pattern", g_variant_new_string(f.pattern.c_str()));
    g_variant_builder_add(&b, "{sv}", "DuplicateData", g_variant_new_boolean(f.duplicateData));
    if (f.discoverable)
        g_variant_builder_add(&b, "{sv}", "Discoverable", g_variant_new_boolean(TRUE));

    callSync(conn_, path_, iface::kAdapter, "SetDiscoveryFilter", g_variant_new("(a{sv})", &b));
}

void Adapter::startDiscovery() { callSync(conn_, path_, iface::kAdapter, "StartDiscovery"); }

void Adapter::stopDiscovery() {
    try {
        callSync(conn_, path_, iface::kAdapter, "StopDiscovery");
    } catch (const DBusError& e) {
        if (e.name() != "org.bluez.Error.Failed" && e.name() != "org.bluez.Error.NotReady") throw;
    }
}

void Adapter::removeDevice(const std::string& devicePath) {
    callSync(conn_, path_, iface::kAdapter, "RemoveDevice", g_variant_new("(o)", devicePath.c_str()));
}

}  // namespace ble
