// SPDX-License-Identifier: MIT
// Private implementation of BluezBackend. Internal header.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "blemgr/bluez_backend.hpp"
#include "gdbus_util.hpp"

namespace blemgr {

namespace iface {
constexpr const char* kAdapter = "org.bluez.Adapter1";
constexpr const char* kDevice = "org.bluez.Device1";
constexpr const char* kGattService = "org.bluez.GattService1";
constexpr const char* kGattChar = "org.bluez.GattCharacteristic1";
constexpr const char* kGattManager = "org.bluez.GattManager1";
constexpr const char* kAgent = "org.bluez.Agent1";
constexpr const char* kAgentManager = "org.bluez.AgentManager1";
constexpr const char* kAdvertisement = "org.bluez.LEAdvertisement1";
constexpr const char* kAdvManager = "org.bluez.LEAdvertisingManager1";
constexpr const char* kProps = "org.freedesktop.DBus.Properties";
constexpr const char* kObjectManager = "org.freedesktop.DBus.ObjectManager";
}  // namespace iface

struct BluezBackend::Impl {
    using ReplyFn = std::function<void(GVariant* reply, const Status& st)>;

    explicit Impl(BluezOptions o) : opts(std::move(o)) {}

    BluezOptions opts;
    IBackendEvents* ev = nullptr;
    GDBusConnection* conn = nullptr;
    GDBusObjectManager* om = nullptr;
    GCancellable* cancel = nullptr;
    GMainContext* ctx = nullptr;
    std::shared_ptr<bool> alive;  // false once stop() ran; guards late callbacks
    std::string adapterPath;
    std::vector<gulong> signalIds;

    // (address|service|char) -> characteristic object path, rebuilt on discovery
    std::map<std::string, std::string> charPaths;
    struct CharOwner {
        std::string address;
        CharRef ref;
    };
    std::map<std::string, CharOwner> charOwners;  // object path -> owner

    // ---- agent
    guint agentReg = 0;

    // ---- advertisement
    struct Adv {
        AdvertisementData data;
        GDBusNodeInfo* node = nullptr;
        guint reg = 0;
        bool registered = false;
    };
    std::unique_ptr<Adv> adv;

    // ---- GATT server
    struct ServerChar {
        Impl* owner = nullptr;
        LocalCharacteristic def;
        std::string path, servicePath, serviceUuid;
        bool notifying = false;
    };
    struct ServerService {
        LocalService def;
        std::string path;
        std::vector<std::unique_ptr<ServerChar>> chars;
    };
    struct Server {
        std::vector<std::unique_ptr<ServerService>> services;
        std::vector<guint> regs;
        std::vector<GDBusNodeInfo*> nodes;
        std::string rootPath;
        bool registered = false;
    };
    std::unique_ptr<Server> server;

    // core
    Status start(IBackendEvents* events);
    void stop();
    void call(const std::string& path, const char* ifc, const char* method, GVariant* params,
              ReplyFn fn, int timeoutMs = -1);
    void defer(std::function<void()> fn);
    void deferStatus(const StatusCallback& cb, Status st);
    void setProperty(const std::string& path, const char* ifc, const char* name, GVariant* value,
                     StatusCallback cb);

    // object manager plumbing
    void onInterfaceAdded(GDBusProxy* proxy);
    void onObjectRemoved(const std::string& path);
    void onInterfaceRemoved(const std::string& path, const std::string& ifc);
    void onPropertiesChanged(GDBusProxy* proxy, GVariant* changed);
    void emitAdapterState();
    AdapterState adapterState();

    // central helpers
    gdbus::ObjPtr<GDBusProxy> proxy(const std::string& path, const char* ifc);
    DeviceInfo deviceInfo(GDBusProxy* dev);
    std::vector<GattServiceInfo> collectServices(const std::string& devPath);
    std::optional<std::string> findCharPath(const std::string& address, const CharRef& ch);
    bool deviceUnderAdapter(const std::string& path) const;

    // agent (bluez_agent.cpp)
    void registerAgent();
    void unregisterAgent();
    static void agentMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                            const gchar* method, GVariant* params, GDBusMethodInvocation* inv,
                            gpointer self);

    // peripheral (bluez_peripheral.cpp)
    void startAdvertising(const AdvertisementData& data, StatusCallback cb);
    void stopAdvertising(StatusCallback cb);
    void registerGattServer(const std::vector<LocalService>& services, StatusCallback cb);
    void unregisterGattServer(StatusCallback cb);
    Status notifyLocal(const CharRef& ch, const Bytes& value);
    void dropAdvertisementObject();
    void dropServerObjects();
};

/// Builds introspection data; aborts on malformed XML (programming error).
GDBusNodeInfo* parseNodeXml(const std::string& xml);

}  // namespace blemgr
