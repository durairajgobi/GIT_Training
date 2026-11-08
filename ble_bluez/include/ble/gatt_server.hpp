// SPDX-License-Identifier: MIT
// Peripheral role: a local GATT application registered with
// org.bluez.GattManager1 and an LE advertisement registered with
// org.bluez.LEAdvertisingManager1, so remote centrals can discover,
// connect and use our services.
#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ble/dbus.hpp"

namespace ble {

struct LocalCharacteristic {
    std::string uuid;
    // Any of: "read", "write", "write-without-response", "notify", "indicate",
    // "encrypt-read", "encrypt-write", "secure-read", ...
    std::vector<std::string> flags;
    std::vector<uint8_t> value;
    // Optional hooks. onRead overrides `value`; onWrite may throw to reject.
    std::function<std::vector<uint8_t>(const std::string& device)> onRead;
    std::function<void(const std::vector<uint8_t>& value, const std::string& device)> onWrite;
};

struct LocalService {
    std::string uuid;
    bool primary = true;
    std::vector<LocalCharacteristic> characteristics;
};

struct CharHandle {
    size_t service = 0;
    size_t characteristic = 0;
};

class GattApplication {
public:
    explicit GattApplication(GDBusConnection* conn, std::string basePath = "/com/example/ble/app");
    ~GattApplication();
    GattApplication(const GattApplication&) = delete;
    GattApplication& operator=(const GattApplication&) = delete;

    // Add services before registerApplication(). Returns the service index.
    size_t addService(const LocalService& svc);

    // Exports all objects and calls GattManager1.RegisterApplication.
    void registerApplication(const std::string& adapterPath, DoneCallback cb);
    void unregisterApplication();

    // Updates a value and sends a notification if a client subscribed.
    void setValue(CharHandle h, const std::vector<uint8_t>& value);
    std::vector<uint8_t> value(CharHandle h) const;
    bool notifying(CharHandle h) const;

private:
    struct ChrNode;
    struct SvcNode;
    struct Node {
        GattApplication* app = nullptr;
        SvcNode* svc = nullptr;
        ChrNode* chr = nullptr;
    };
    struct ChrNode {
        std::string path;
        LocalCharacteristic def;
        bool notifying = false;
        guint regId = 0;
        Node node;
    };
    struct SvcNode {
        std::string path;
        std::string uuid;
        bool primary = true;
        std::vector<std::unique_ptr<ChrNode>> chars;
        guint regId = 0;
        Node node;
    };

    void exportObjects();
    void unexportObjects();
    GVariant* serviceProperty(const SvcNode& s, const std::string& name) const;
    GVariant* charProperty(const ChrNode& c, const std::string& name) const;
    GVariant* managedObjects() const;
    void emitChanged(const ChrNode& c, const char* prop);
    ChrNode& chr(CharHandle h) const;

    static void onMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                         GVariant*, GDBusMethodInvocation*, gpointer);
    static GVariant* onGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                   const gchar*, GError**, gpointer);

    GDBusConnection* conn_;
    std::string basePath_;
    std::string adapterPath_;
    GDBusNodeInfo* introspection_ = nullptr;
    std::vector<std::unique_ptr<SvcNode>> services_;
    guint rootRegId_ = 0;
    Node rootNode_;
    bool registered_ = false;
};

class Advertisement {
public:
    explicit Advertisement(GDBusConnection* conn,
                           std::string path = "/com/example/ble/advertisement0");
    ~Advertisement();
    Advertisement(const Advertisement&) = delete;
    Advertisement& operator=(const Advertisement&) = delete;

    std::string type = "peripheral";          // or "broadcast"
    std::string localName;
    std::vector<std::string> serviceUuids;
    std::map<uint16_t, std::vector<uint8_t>> manufacturerData;
    uint16_t appearance = 0;
    bool includeTxPower = false;
    bool discoverable = true;

    void registerAdvertisement(const std::string& adapterPath, DoneCallback cb);
    void unregisterAdvertisement();

private:
    static void onMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                         GVariant*, GDBusMethodInvocation*, gpointer);
    static GVariant* onGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                   const gchar*, GError**, gpointer);

    GDBusConnection* conn_;
    std::string path_;
    std::string adapterPath_;
    guint regId_ = 0;
    bool registered_ = false;
};

}  // namespace ble
