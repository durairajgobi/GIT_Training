// SPDX-License-Identifier: MIT
// Thin RAII / convenience layer over GDBus for talking to bluetoothd.
#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ble {

namespace iface {
constexpr const char* kBluezService   = "org.bluez";
constexpr const char* kAdapter        = "org.bluez.Adapter1";
constexpr const char* kDevice         = "org.bluez.Device1";
constexpr const char* kAgent          = "org.bluez.Agent1";
constexpr const char* kAgentManager   = "org.bluez.AgentManager1";
constexpr const char* kGattService    = "org.bluez.GattService1";
constexpr const char* kGattChrc       = "org.bluez.GattCharacteristic1";
constexpr const char* kGattDesc       = "org.bluez.GattDescriptor1";
constexpr const char* kGattManager    = "org.bluez.GattManager1";
constexpr const char* kAdvertisement  = "org.bluez.LEAdvertisement1";
constexpr const char* kAdvManager     = "org.bluez.LEAdvertisingManager1";
constexpr const char* kProperties     = "org.freedesktop.DBus.Properties";
constexpr const char* kObjectManager  = "org.freedesktop.DBus.ObjectManager";
}  // namespace iface

// Exception carrying the D-Bus error name (e.g. "org.bluez.Error.Failed").
class DBusError : public std::runtime_error {
public:
    DBusError(const std::string& name, const std::string& message)
        : std::runtime_error(name.empty() ? message : name + ": " + message), name_(name) {}
    const std::string& name() const { return name_; }

private:
    std::string name_;
};

struct VariantUnref {
    void operator()(GVariant* v) const { if (v) g_variant_unref(v); }
};
using VariantPtr = std::unique_ptr<GVariant, VariantUnref>;

// Takes ownership of a (possibly floating) variant.
inline VariantPtr adopt(GVariant* v) { return VariantPtr(v ? g_variant_ref_sink(v) : nullptr); }

// Converts a GError into a DBusError and frees it. No-op if err == nullptr.
void throwIfError(GError* err, const std::string& context);

// Splits a GError into (dbus error name, message) and frees it.
std::pair<std::string, std::string> takeError(GError* err);

// Result of an asynchronous call: on failure errorName/errorMessage are set.
struct CallResult {
    bool ok = false;
    std::string errorName;
    std::string errorMessage;
    VariantPtr reply;
};
using CallCallback = std::function<void(CallResult)>;
using DoneCallback = std::function<void(bool ok, const std::string& error)>;

VariantPtr callSync(GDBusConnection* conn, const std::string& path, const char* interface,
                    const char* method, GVariant* params = nullptr,
                    const GVariantType* replyType = nullptr, int timeoutMs = -1);

// Asynchronous call on org.bluez. Must be used for anything that can call
// back into this process (Pair, RegisterApplication, ...) because a
// synchronous call does not iterate the main loop.
void callAsync(GDBusConnection* conn, const std::string& path, const char* interface,
               const char* method, GVariant* params, int timeoutMs, CallCallback cb);

void setProperty(GDBusConnection* conn, const std::string& path, const char* interface,
                 const char* name, GVariant* value);
VariantPtr getProperty(GDBusConnection* conn, const std::string& path, const char* interface,
                       const char* name);

// a{sv} helpers --------------------------------------------------------------
using PropertyMap = std::map<std::string, VariantPtr>;
PropertyMap toPropertyMap(GVariant* asv);

std::string        variantString(GVariant* v, const std::string& def = "");
bool               variantBool(GVariant* v, bool def = false);
std::vector<std::string> variantStringList(GVariant* v);
std::vector<uint8_t> variantBytes(GVariant* v);
GVariant*          bytesVariant(const std::vector<uint8_t>& data);  // floating "ay"
GVariant*          stringListVariant(const std::vector<std::string>& list);  // floating "as"

// Runs fn after `ms` on the default main context; fn returns true to repeat.
guint addTimeout(unsigned ms, std::function<bool()> fn);
// Runs fn once on the next main-loop iteration.
guint addIdle(std::function<void()> fn);

}  // namespace ble
