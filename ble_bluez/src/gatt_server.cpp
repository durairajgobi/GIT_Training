// SPDX-License-Identifier: MIT
#include "ble/gatt_server.hpp"

#include <algorithm>
#include <stdexcept>

#include "ble/dbus.hpp"
#include "ble/utils.hpp"

namespace ble {
namespace {

const char* kAppXml = R"XML(
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects">
      <arg type="a{oa{sa{sv}}}" name="objects" direction="out"/>
    </method>
  </interface>
  <interface name="org.bluez.GattService1">
    <property name="UUID" type="s" access="read"/>
    <property name="Primary" type="b" access="read"/>
    <property name="Characteristics" type="ao" access="read"/>
  </interface>
  <interface name="org.bluez.GattCharacteristic1">
    <method name="ReadValue">
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="ay" name="value" direction="out"/>
    </method>
    <method name="WriteValue">
      <arg type="ay" name="value" direction="in"/>
      <arg type="a{sv}" name="options" direction="in"/>
    </method>
    <method name="StartNotify"/>
    <method name="StopNotify"/>
    <property name="UUID" type="s" access="read"/>
    <property name="Service" type="o" access="read"/>
    <property name="Flags" type="as" access="read"/>
    <property name="Value" type="ay" access="read"/>
    <property name="Notifying" type="b" access="read"/>
  </interface>
</node>)XML";

const char* kAdvXml = R"XML(
<node>
  <interface name="org.bluez.LEAdvertisement1">
    <method name="Release"/>
    <property name="Type" type="s" access="read"/>
    <property name="ServiceUUIDs" type="as" access="read"/>
    <property name="ManufacturerData" type="a{qv}" access="read"/>
    <property name="LocalName" type="s" access="read"/>
    <property name="Appearance" type="q" access="read"/>
    <property name="Includes" type="as" access="read"/>
    <property name="Discoverable" type="b" access="read"/>
  </interface>
</node>)XML";

std::string optionString(GVariant* options, const char* key) {
    std::string out;
    if (GVariant* v = g_variant_lookup_value(options, key, nullptr)) {
        out = variantString(v);
        g_variant_unref(v);
    }
    return out;
}

uint16_t optionOffset(GVariant* options) {
    uint16_t off = 0;
    g_variant_lookup(options, "offset", "q", &off);
    return off;
}

void returnError(GDBusMethodInvocation* inv, const char* name, const std::string& msg) {
    g_dbus_method_invocation_return_dbus_error(inv, name, msg.c_str());
}

}  // namespace

// ============================================================================
// GattApplication
// ============================================================================

GattApplication::GattApplication(GDBusConnection* conn, std::string basePath)
    : conn_(conn), basePath_(std::move(basePath)) {
    GError* err = nullptr;
    introspection_ = g_dbus_node_info_new_for_xml(kAppXml, &err);
    throwIfError(err, "parsing GATT introspection");
    rootNode_.app = this;
}

GattApplication::~GattApplication() {
    try {
        unregisterApplication();
    } catch (...) {
    }
    unexportObjects();
    if (introspection_) g_dbus_node_info_unref(introspection_);
}

size_t GattApplication::addService(const LocalService& def) {
    if (rootRegId_) throw std::logic_error("addService() after registerApplication()");
    auto svc = std::make_unique<SvcNode>();
    svc->path = basePath_ + "/service" + std::to_string(services_.size());
    svc->uuid = normalizeUuid(def.uuid);
    svc->primary = def.primary;
    svc->node = Node{this, svc.get(), nullptr};
    for (const auto& c : def.characteristics) {
        auto chr = std::make_unique<ChrNode>();
        chr->path = svc->path + "/char" + std::to_string(svc->chars.size());
        chr->def = c;
        chr->def.uuid = normalizeUuid(c.uuid);
        chr->node = Node{this, svc.get(), chr.get()};
        svc->chars.push_back(std::move(chr));
    }
    services_.push_back(std::move(svc));
    return services_.size() - 1;
}

void GattApplication::exportObjects() {
    if (rootRegId_) return;
    static const GDBusInterfaceVTable vtable = {onMethod, onGetProperty, nullptr, {}};
    auto* omIface = g_dbus_node_info_lookup_interface(introspection_, iface::kObjectManager);
    auto* svcIface = g_dbus_node_info_lookup_interface(introspection_, iface::kGattService);
    auto* chrIface = g_dbus_node_info_lookup_interface(introspection_, iface::kGattChrc);

    GError* err = nullptr;
    rootRegId_ = g_dbus_connection_register_object(conn_, basePath_.c_str(), omIface, &vtable,
                                                   &rootNode_, nullptr, &err);
    throwIfError(err, "exporting " + basePath_);
    for (auto& s : services_) {
        s->regId = g_dbus_connection_register_object(conn_, s->path.c_str(), svcIface, &vtable,
                                                     &s->node, nullptr, &err);
        throwIfError(err, "exporting " + s->path);
        for (auto& c : s->chars) {
            c->regId = g_dbus_connection_register_object(conn_, c->path.c_str(), chrIface, &vtable,
                                                         &c->node, nullptr, &err);
            throwIfError(err, "exporting " + c->path);
        }
    }
}

void GattApplication::unexportObjects() {
    for (auto& s : services_) {
        for (auto& c : s->chars)
            if (c->regId) { g_dbus_connection_unregister_object(conn_, c->regId); c->regId = 0; }
        if (s->regId) { g_dbus_connection_unregister_object(conn_, s->regId); s->regId = 0; }
    }
    if (rootRegId_) { g_dbus_connection_unregister_object(conn_, rootRegId_); rootRegId_ = 0; }
}

void GattApplication::registerApplication(const std::string& adapterPath, DoneCallback cb) {
    exportObjects();
    adapterPath_ = adapterPath;
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    // Async: bluetoothd calls our GetManagedObjects before replying.
    callAsync(conn_, adapterPath, iface::kGattManager, "RegisterApplication",
              g_variant_new("(oa{sv})", basePath_.c_str(), &opts), 20000,
              [this, cb = std::move(cb)](CallResult r) {
                  registered_ = r.ok;
                  if (cb) cb(r.ok, r.ok ? "" : r.errorName + ": " + r.errorMessage);
              });
}

void GattApplication::unregisterApplication() {
    if (!registered_) return;
    registered_ = false;
    callSync(conn_, adapterPath_, iface::kGattManager, "UnregisterApplication",
             g_variant_new("(o)", basePath_.c_str()));
}

GattApplication::ChrNode& GattApplication::chr(CharHandle h) const {
    if (h.service >= services_.size() || h.characteristic >= services_[h.service]->chars.size())
        throw std::out_of_range("invalid characteristic handle");
    return *services_[h.service]->chars[h.characteristic];
}

void GattApplication::setValue(CharHandle h, const std::vector<uint8_t>& value) {
    ChrNode& c = chr(h);
    c.def.value = value;
    if (c.notifying && c.regId) emitChanged(c, "Value");
}

std::vector<uint8_t> GattApplication::value(CharHandle h) const { return chr(h).def.value; }
bool GattApplication::notifying(CharHandle h) const { return chr(h).notifying; }

GVariant* GattApplication::serviceProperty(const SvcNode& s, const std::string& name) const {
    if (name == "UUID") return g_variant_new_string(s.uuid.c_str());
    if (name == "Primary") return g_variant_new_boolean(s.primary);
    if (name == "Characteristics") {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ao"));
        for (const auto& c : s.chars) g_variant_builder_add(&b, "o", c->path.c_str());
        return g_variant_builder_end(&b);
    }
    return nullptr;
}

GVariant* GattApplication::charProperty(const ChrNode& c, const std::string& name) const {
    if (name == "UUID") return g_variant_new_string(c.def.uuid.c_str());
    if (name == "Service") {
        return g_variant_new_object_path(c.node.svc->path.c_str());
    }
    if (name == "Flags") return stringListVariant(c.def.flags);
    if (name == "Value") return bytesVariant(c.def.value);
    if (name == "Notifying") return g_variant_new_boolean(c.notifying);
    return nullptr;
}

GVariant* GattApplication::managedObjects() const {
    GVariantBuilder objects;
    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    auto addObject = [&](const std::string& path, const char* ifaceName,
                         std::initializer_list<const char*> names, auto&& getter) {
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        for (const char* n : names)
            if (GVariant* v = getter(n)) g_variant_builder_add(&props, "{sv}", n, v);
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&ifaces, "{sa{sv}}", ifaceName, &props);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", path.c_str(), &ifaces);
    };
    for (const auto& s : services_) {
        addObject(s->path, iface::kGattService, {"UUID", "Primary", "Characteristics"},
                  [&](const char* n) { return serviceProperty(*s, n); });
        for (const auto& c : s->chars)
            addObject(c->path, iface::kGattChrc, {"UUID", "Service", "Flags", "Value", "Notifying"},
                      [&](const char* n) { return charProperty(*c, n); });
    }
    return g_variant_builder_end(&objects);
}

void GattApplication::emitChanged(const ChrNode& c, const char* prop) {
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", prop, charProperty(c, prop));
    g_dbus_connection_emit_signal(conn_, nullptr, c.path.c_str(), iface::kProperties,
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}@as)", iface::kGattChrc, &changed,
                                                g_variant_new_strv(nullptr, 0)),
                                  nullptr);
}

void GattApplication::onMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                               const gchar* method, GVariant* params, GDBusMethodInvocation* inv,
                               gpointer data) {
    auto* node = static_cast<Node*>(data);
    GattApplication* app = node->app;
    std::string m = method;

    if (m == "GetManagedObjects") {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{oa{sa{sv}}})",
                                                                 app->managedObjects()));
        return;
    }
    ChrNode* c = node->chr;
    if (!c) return returnError(inv, "org.bluez.Error.NotSupported", m);
    auto has = [&](std::initializer_list<const char*> fl) {
        for (const char* f : fl)
            if (std::find(c->def.flags.begin(), c->def.flags.end(), f) != c->def.flags.end())
                return true;
        return false;
    };

    try {
        if (m == "ReadValue") {
            if (!has({"read", "encrypt-read", "encrypt-authenticated-read", "secure-read"}))
                return returnError(inv, "org.bluez.Error.NotPermitted", "read not permitted");
            GVariant* options = g_variant_get_child_value(params, 0);
            std::string device = optionString(options, "device");
            uint16_t offset = optionOffset(options);
            g_variant_unref(options);
            if (c->def.onRead) c->def.value = c->def.onRead(device);
            if (offset > c->def.value.size())
                return returnError(inv, "org.bluez.Error.InvalidOffset", "offset out of range");
            std::vector<uint8_t> out(c->def.value.begin() + offset, c->def.value.end());
            g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", bytesVariant(out)));
        } else if (m == "WriteValue") {
            if (!has({"write", "write-without-response", "encrypt-write",
                      "encrypt-authenticated-write", "secure-write"}))
                return returnError(inv, "org.bluez.Error.NotPermitted", "write not permitted");
            GVariant* ay = g_variant_get_child_value(params, 0);
            GVariant* options = g_variant_get_child_value(params, 1);
            auto bytes = variantBytes(ay);
            std::string device = optionString(options, "device");
            uint16_t offset = optionOffset(options);
            g_variant_unref(ay);
            g_variant_unref(options);
            if (offset > c->def.value.size())
                return returnError(inv, "org.bluez.Error.InvalidOffset", "offset out of range");
            std::vector<uint8_t> next(c->def.value.begin(), c->def.value.begin() + offset);
            next.insert(next.end(), bytes.begin(), bytes.end());
            if (c->def.onWrite) c->def.onWrite(next, device);
            c->def.value = next;
            g_dbus_method_invocation_return_value(inv, nullptr);
            if (c->notifying) app->emitChanged(*c, "Value");
        } else if (m == "StartNotify") {
            if (!has({"notify", "indicate"}))
                return returnError(inv, "org.bluez.Error.NotSupported", "notify not supported");
            c->notifying = true;
            g_message("client subscribed to %s", c->def.uuid.c_str());
            g_dbus_method_invocation_return_value(inv, nullptr);
            app->emitChanged(*c, "Notifying");
        } else if (m == "StopNotify") {
            c->notifying = false;
            g_message("client unsubscribed from %s", c->def.uuid.c_str());
            g_dbus_method_invocation_return_value(inv, nullptr);
            app->emitChanged(*c, "Notifying");
        } else {
            returnError(inv, "org.freedesktop.DBus.Error.UnknownMethod", m);
        }
    } catch (const std::exception& e) {
        returnError(inv, "org.bluez.Error.Failed", e.what());
    }
}

GVariant* GattApplication::onGetProperty(GDBusConnection*, const gchar*, const gchar*,
                                         const gchar*, const gchar* name, GError** error,
                                         gpointer data) {
    auto* node = static_cast<Node*>(data);
    GVariant* v = nullptr;
    if (node->chr) v = node->app->charProperty(*node->chr, name);
    else if (node->svc) v = node->app->serviceProperty(*node->svc, name);
    if (!v) g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "no property %s", name);
    return v;
}

// ============================================================================
// Advertisement
// ============================================================================

Advertisement::Advertisement(GDBusConnection* conn, std::string path)
    : conn_(conn), path_(std::move(path)) {}

Advertisement::~Advertisement() {
    try {
        unregisterAdvertisement();
    } catch (...) {
    }
    if (regId_) g_dbus_connection_unregister_object(conn_, regId_);
}

void Advertisement::registerAdvertisement(const std::string& adapterPath, DoneCallback cb) {
    if (!regId_) {
        GError* err = nullptr;
        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kAdvXml, &err);
        throwIfError(err, "parsing advertisement introspection");
        static const GDBusInterfaceVTable vtable = {onMethod, onGetProperty, nullptr, {}};
        regId_ = g_dbus_connection_register_object(conn_, path_.c_str(), node->interfaces[0],
                                                   &vtable, this, nullptr, &err);
        g_dbus_node_info_unref(node);
        throwIfError(err, "exporting advertisement");
    }
    adapterPath_ = adapterPath;
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    callAsync(conn_, adapterPath, iface::kAdvManager, "RegisterAdvertisement",
              g_variant_new("(oa{sv})", path_.c_str(), &opts), 20000,
              [this, cb = std::move(cb)](CallResult r) {
                  registered_ = r.ok;
                  if (cb) cb(r.ok, r.ok ? "" : r.errorName + ": " + r.errorMessage);
              });
}

void Advertisement::unregisterAdvertisement() {
    if (!registered_) return;
    registered_ = false;
    callSync(conn_, adapterPath_, iface::kAdvManager, "UnregisterAdvertisement",
             g_variant_new("(o)", path_.c_str()));
}

void Advertisement::onMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                             const gchar* method, GVariant*, GDBusMethodInvocation* inv,
                             gpointer data) {
    if (std::string(method) == "Release") {
        static_cast<Advertisement*>(data)->registered_ = false;
        g_message("advertisement released by bluetoothd");
    }
    g_dbus_method_invocation_return_value(inv, nullptr);
}

GVariant* Advertisement::onGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                       const gchar* name, GError** error, gpointer data) {
    auto* self = static_cast<Advertisement*>(data);
    std::string n = name;
    GVariant* v = nullptr;
    // Returning NULL omits the property from GetAll, which is how optional
    // advertisement fields are left out.
    if (n == "Type") {
        v = g_variant_new_string(self->type.c_str());
    } else if (n == "ServiceUUIDs" && !self->serviceUuids.empty()) {
        std::vector<std::string> uuids;
        for (const auto& u : self->serviceUuids) uuids.push_back(normalizeUuid(u));
        v = stringListVariant(uuids);
    } else if (n == "ManufacturerData" && !self->manufacturerData.empty()) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{qv}"));
        for (const auto& [id, data] : self->manufacturerData)
            g_variant_builder_add(&b, "{qv}", id, bytesVariant(data));
        v = g_variant_builder_end(&b);
    } else if (n == "LocalName" && !self->localName.empty()) {
        v = g_variant_new_string(self->localName.c_str());
    } else if (n == "Appearance" && self->appearance) {
        v = g_variant_new_uint16(self->appearance);
    } else if (n == "Includes" && self->includeTxPower) {
        v = stringListVariant({"tx-power"});
    } else if (n == "Discoverable") {
        v = g_variant_new_boolean(self->discoverable);
    }
    if (!v) g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "%s not set", name);
    return v;
}

}  // namespace ble
