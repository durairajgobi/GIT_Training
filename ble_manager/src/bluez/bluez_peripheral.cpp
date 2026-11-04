// SPDX-License-Identifier: MIT
// BlueZ backend, peripheral role: LE advertisement and local GATT server
// objects exported on D-Bus and registered with LEAdvertisingManager1 /
// GattManager1.
#include <algorithm>

#include "blemgr/log.hpp"
#include "bluez_impl.hpp"

namespace blemgr {

using namespace gdbus;

namespace {

GVariant* emptyDict() { return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0); }

std::string advPath(const BluezOptions& o) { return o.objectRoot + "/advertisement0"; }

uint16_t optionOffset(GVariant* options) {
    guint16 off = 0;
    if (options) g_variant_lookup(options, "offset", "q", &off);
    return off;
}

// ------------------------------------------------------------ advertisement

GVariant* advProperty(const AdvertisementData& a, const std::string& name) {
    if (name == "Type") return g_variant_new_string(a.connectable ? "peripheral" : "broadcast");
    if (name == "ServiceUUIDs") return fromStrings(a.serviceUuids);
    if (name == "LocalName") return g_variant_new_string(a.localName.c_str());
    if (name == "Includes") return fromStrings({"tx-power"});
    if (name == "Appearance") return g_variant_new_uint16(a.appearance.value_or(0));
    if (name == "ManufacturerData") {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{qv}"));
        for (const auto& [id, data] : a.manufacturerData)
            g_variant_builder_add(&b, "{qv}", id, fromBytes(data));
        return g_variant_builder_end(&b);
    }
    if (name == "ServiceData") {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
        for (const auto& [uuid, data] : a.serviceData)
            g_variant_builder_add(&b, "{sv}", uuid.c_str(), fromBytes(data));
        return g_variant_builder_end(&b);
    }
    return nullptr;
}

std::string advXml(const AdvertisementData& a) {
    std::string x =
        "<node><interface name='org.bluez.LEAdvertisement1'><method name='Release'/>"
        "<property name='Type' type='s' access='read'/>";
    // Only advertise what is set: BlueZ rejects empty values for some fields.
    if (!a.serviceUuids.empty()) x += "<property name='ServiceUUIDs' type='as' access='read'/>";
    if (!a.manufacturerData.empty())
        x += "<property name='ManufacturerData' type='a{qv}' access='read'/>";
    if (!a.serviceData.empty()) x += "<property name='ServiceData' type='a{sv}' access='read'/>";
    if (!a.localName.empty()) x += "<property name='LocalName' type='s' access='read'/>";
    if (a.includeTxPower) x += "<property name='Includes' type='as' access='read'/>";
    if (a.appearance) x += "<property name='Appearance' type='q' access='read'/>";
    return x + "</interface></node>";
}

// -------------------------------------------------------------- GATT server

constexpr const char* kObjectManagerXml = R"XML(
<node>
  <interface name='org.freedesktop.DBus.ObjectManager'>
    <method name='GetManagedObjects'>
      <arg type='a{oa{sa{sv}}}' direction='out'/>
    </method>
  </interface>
</node>)XML";

constexpr const char* kServiceXml = R"XML(
<node>
  <interface name='org.bluez.GattService1'>
    <property name='UUID' type='s' access='read'/>
    <property name='Primary' type='b' access='read'/>
  </interface>
</node>)XML";

constexpr const char* kCharXml = R"XML(
<node>
  <interface name='org.bluez.GattCharacteristic1'>
    <method name='ReadValue'>
      <arg type='a{sv}' direction='in'/><arg type='ay' direction='out'/>
    </method>
    <method name='WriteValue'>
      <arg type='ay' direction='in'/><arg type='a{sv}' direction='in'/>
    </method>
    <method name='StartNotify'/>
    <method name='StopNotify'/>
    <property name='UUID' type='s' access='read'/>
    <property name='Service' type='o' access='read'/>
    <property name='Flags' type='as' access='read'/>
    <property name='Value' type='ay' access='read'/>
    <property name='Notifying' type='b' access='read'/>
  </interface>
</node>)XML";

using ServerChar = BluezBackend::Impl::ServerChar;
using ServerService = BluezBackend::Impl::ServerService;
using Server = BluezBackend::Impl::Server;

GVariant* serviceProperty(const ServerService& s, const std::string& name) {
    if (name == "UUID") return g_variant_new_string(s.def.uuid.c_str());
    if (name == "Primary") return g_variant_new_boolean(s.def.primary);
    return nullptr;
}

GVariant* charProperty(const ServerChar& c, const std::string& name) {
    if (name == "UUID") return g_variant_new_string(c.def.uuid.c_str());
    if (name == "Service") return g_variant_new_object_path(c.servicePath.c_str());
    if (name == "Flags") return fromStrings(charPropertyFlags(c.def.properties));
    if (name == "Value") return fromBytes(c.def.value);
    if (name == "Notifying") return g_variant_new_boolean(c.notifying);
    return nullptr;
}

void addProps(GVariantBuilder* ifaces, const char* ifc, const std::vector<const char*>& names,
              const std::function<GVariant*(const std::string&)>& get) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    for (const char* n : names) g_variant_builder_add(&props, "{sv}", n, get(n));
    g_variant_builder_add(ifaces, "{sa{sv}}", ifc, &props);
}

void managedObjects(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                    GVariant*, GDBusMethodInvocation* inv, gpointer user) {
    auto* srv = static_cast<Server*>(user);
    GVariantBuilder objs;
    g_variant_builder_init(&objs, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    for (const auto& s : srv->services) {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        addProps(&ifaces, iface::kGattService, {"UUID", "Primary"},
                 [&](const std::string& n) { return serviceProperty(*s, n); });
        g_variant_builder_add(&objs, "{oa{sa{sv}}}", s->path.c_str(), &ifaces);
        for (const auto& c : s->chars) {
            GVariantBuilder ci;
            g_variant_builder_init(&ci, G_VARIANT_TYPE("a{sa{sv}}"));
            addProps(&ci, iface::kGattChar, {"UUID", "Service", "Flags", "Value", "Notifying"},
                     [&](const std::string& n) { return charProperty(*c, n); });
            g_variant_builder_add(&objs, "{oa{sa{sv}}}", c->path.c_str(), &ci);
        }
    }
    g_dbus_method_invocation_return_value(inv, g_variant_new("(a{oa{sa{sv}}})", &objs));
}

void charMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                GVariant* params, GDBusMethodInvocation* inv, gpointer user) {
    auto* c = static_cast<ServerChar*>(user);
    const std::string m = method;
    const uint32_t p = c->def.properties;

    if (m == "ReadValue") {
        if (!(p & (kPropRead | kPropEncryptRead)))
            return g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotPermitted", "read");
        VariantPtr opts(g_variant_get_child_value(params, 0));
        const uint16_t off = optionOffset(opts.get());
        if (c->def.onRead) c->def.value = c->def.onRead();
        if (off > c->def.value.size())
            return g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.InvalidOffset", "offset");
        Bytes slice(c->def.value.begin() + off, c->def.value.end());
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", fromBytes(slice)));
    } else if (m == "WriteValue") {
        if (!(p & (kPropWrite | kPropWriteNoResponse | kPropEncryptWrite)))
            return g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotPermitted", "write");
        VariantPtr data(g_variant_get_child_value(params, 0));
        VariantPtr opts(g_variant_get_child_value(params, 1));
        const uint16_t off = optionOffset(opts.get());
        Bytes in = toBytes(data.get());
        Bytes next = c->def.value;
        if (off > next.size())
            return g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.InvalidOffset", "offset");
        if (off == 0) {
            next = in;
        } else {
            next.resize(std::max(next.size(), static_cast<size_t>(off) + in.size()));
            std::copy(in.begin(), in.end(), next.begin() + off);
        }
        if (c->def.onWrite) {
            Status st = c->def.onWrite(next);
            if (!st.ok()) {
                const char* name = st.code() == Error::InvalidArgument
                                       ? "org.bluez.Error.InvalidValueLength"
                                       : "org.bluez.Error.NotPermitted";
                return g_dbus_method_invocation_return_dbus_error(inv, name, st.message().c_str());
            }
        }
        c->def.value = std::move(next);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (m == "StartNotify" || m == "StopNotify") {
        if (!(p & (kPropNotify | kPropIndicate)))
            return g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotSupported", "notify");
        c->notifying = (m == "StartNotify");
        BLE_LOGI("local %s: notifications %s", c->def.uuid.c_str(), c->notifying ? "on" : "off");
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotSupported", method);
    }
}

}  // namespace

// ================================================================ advertising

void BluezBackend::Impl::dropAdvertisementObject() {
    if (!adv) return;
    if (adv->reg && conn) g_dbus_connection_unregister_object(conn, adv->reg);
    if (adv->node) g_dbus_node_info_unref(adv->node);
    adv.reset();
}

void BluezBackend::Impl::startAdvertising(const AdvertisementData& data, StatusCallback cb) {
    if (!conn) return cb({Error::NotReady, "backend not started"});
    auto doRegister = [this, data, cb] {
        dropAdvertisementObject();
        adv = std::make_unique<Adv>();
        adv->data = data;
        adv->node = parseNodeXml(advXml(data));

        GDBusInterfaceVTable vt{};
        vt.method_call = [](GDBusConnection*, const gchar*, const gchar*, const gchar*,
                            const gchar* method, GVariant*, GDBusMethodInvocation* inv, gpointer self) {
            auto* d = static_cast<Impl*>(self);
            BLE_LOGI("advertisement %s by BlueZ", method);
            if (d->adv) d->adv->registered = false;
            g_dbus_method_invocation_return_value(inv, nullptr);
        };
        vt.get_property = [](GDBusConnection*, const gchar*, const gchar*, const gchar*,
                             const gchar* name, GError** error, gpointer self) -> GVariant* {
            auto* d = static_cast<Impl*>(self);
            GVariant* v = d->adv ? advProperty(d->adv->data, name) : nullptr;
            if (!v) g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "%s", name);
            return v;
        };
        GError* raw = nullptr;
        const std::string path = advPath(opts);
        adv->reg = g_dbus_connection_register_object(conn, path.c_str(), adv->node->interfaces[0],
                                                     &vt, this, nullptr, &raw);
        if (!adv->reg) {
            ErrorPtr err(raw);
            dropAdvertisementObject();
            return cb({Error::Failed, std::string("export advertisement: ") + err->message});
        }
        call(adapterPath, iface::kAdvManager, "RegisterAdvertisement",
             g_variant_new("(o@a{sv})", path.c_str(), emptyDict()),
             [this, cb](GVariant*, const Status& s) {
                 if (s.ok()) {
                     if (adv) adv->registered = true;
                     BLE_LOGI("advertising started");
                 } else {
                     dropAdvertisementObject();
                 }
                 cb(s);
             });
    };

    if (adv && adv->registered) {
        // Replace the running advertisement.
        call(adapterPath, iface::kAdvManager, "UnregisterAdvertisement",
             g_variant_new("(o)", advPath(opts).c_str()),
             [doRegister](GVariant*, const Status&) { doRegister(); });
    } else {
        doRegister();
    }
}

void BluezBackend::Impl::stopAdvertising(StatusCallback cb) {
    if (!adv) return deferStatus(cb, {});
    const bool wasRegistered = adv->registered;
    dropAdvertisementObject();
    if (!wasRegistered) return deferStatus(cb, {});
    call(adapterPath, iface::kAdvManager, "UnregisterAdvertisement",
         g_variant_new("(o)", advPath(opts).c_str()), [cb](GVariant*, const Status& s) {
             BLE_LOGI("advertising stopped");
             cb(s);
         });
}

// ================================================================ GATT server

void BluezBackend::Impl::dropServerObjects() {
    if (!server) return;
    if (conn)
        for (guint id : server->regs) g_dbus_connection_unregister_object(conn, id);
    server.reset();
}

void BluezBackend::Impl::registerGattServer(const std::vector<LocalService>& services,
                                            StatusCallback cb) {
    if (!conn) return cb({Error::NotReady, "backend not started"});
    auto doRegister = [this, services, cb] {
        dropServerObjects();
        static GDBusNodeInfo* omNode = parseNodeXml(kObjectManagerXml);
        static GDBusNodeInfo* svcNode = parseNodeXml(kServiceXml);
        static GDBusNodeInfo* charNode = parseNodeXml(kCharXml);

        server = std::make_unique<Server>();
        server->rootPath = opts.objectRoot + "/app";
        for (size_t i = 0; i < services.size(); ++i) {
            auto s = std::make_unique<ServerService>();
            s->def = services[i];
            s->path = server->rootPath + "/service" + std::to_string(i);
            for (size_t j = 0; j < s->def.characteristics.size(); ++j) {
                auto c = std::make_unique<ServerChar>();
                c->owner = this;
                c->def = s->def.characteristics[j];
                c->path = s->path + "/char" + std::to_string(j);
                c->servicePath = s->path;
                c->serviceUuid = s->def.uuid;
                s->chars.push_back(std::move(c));
            }
            server->services.push_back(std::move(s));
        }

        auto exportObj = [this](const std::string& path, GDBusInterfaceInfo* info,
                                const GDBusInterfaceVTable* vt, gpointer user) {
            GError* raw = nullptr;
            guint id = g_dbus_connection_register_object(conn, path.c_str(), info, vt, user,
                                                         nullptr, &raw);
            if (!id) {
                ErrorPtr err(raw);
                BLE_LOGE("export %s: %s", path.c_str(), err->message);
                return false;
            }
            server->regs.push_back(id);
            return true;
        };

        static GDBusInterfaceVTable omVt{};
        omVt.method_call = &managedObjects;
        static GDBusInterfaceVTable svcVt{};
        svcVt.get_property = [](GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                const gchar* name, GError** error, gpointer user) -> GVariant* {
            GVariant* v = serviceProperty(*static_cast<ServerService*>(user), name);
            if (!v) g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "%s", name);
            return v;
        };
        static GDBusInterfaceVTable charVt{};
        charVt.method_call = &charMethod;
        charVt.get_property = [](GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                 const gchar* name, GError** error, gpointer user) -> GVariant* {
            GVariant* v = charProperty(*static_cast<ServerChar*>(user), name);
            if (!v) g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "%s", name);
            return v;
        };

        bool ok = exportObj(server->rootPath, omNode->interfaces[0], &omVt, server.get());
        for (auto& s : server->services) {
            ok = ok && exportObj(s->path, svcNode->interfaces[0], &svcVt, s.get());
            for (auto& c : s->chars) ok = ok && exportObj(c->path, charNode->interfaces[0], &charVt, c.get());
        }
        if (!ok) {
            dropServerObjects();
            return cb({Error::Failed, "exporting GATT objects failed"});
        }

        call(adapterPath, iface::kGattManager, "RegisterApplication",
             g_variant_new("(o@a{sv})", server->rootPath.c_str(), emptyDict()),
             [this, cb](GVariant*, const Status& s) {
                 if (s.ok()) {
                     if (server) server->registered = true;
                     BLE_LOGI("GATT server registered");
                 } else {
                     dropServerObjects();
                 }
                 cb(s);
             });
    };

    if (server && server->registered) {
        call(adapterPath, iface::kGattManager, "UnregisterApplication",
             g_variant_new("(o)", server->rootPath.c_str()),
             [doRegister](GVariant*, const Status&) { doRegister(); });
    } else {
        doRegister();
    }
}

void BluezBackend::Impl::unregisterGattServer(StatusCallback cb) {
    if (!server) return deferStatus(cb, {});
    const bool wasRegistered = server->registered;
    const std::string root = server->rootPath;
    dropServerObjects();
    if (!wasRegistered) return deferStatus(cb, {});
    call(adapterPath, iface::kGattManager, "UnregisterApplication", g_variant_new("(o)", root.c_str()),
         [cb](GVariant*, const Status& s) { cb(s); });
}

Status BluezBackend::Impl::notifyLocal(const CharRef& ch, const Bytes& value) {
    if (!server || !conn) return {Error::NotReady, "no GATT server"};
    for (auto& s : server->services) {
        if (s->def.uuid != ch.service) continue;
        for (auto& c : s->chars) {
            if (c->def.uuid != ch.characteristic) continue;
            c->def.value = value;
            if (!c->notifying) return {};
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&b, "{sv}", "Value", fromBytes(value));
            GError* raw = nullptr;
            g_dbus_connection_emit_signal(
                conn, nullptr, c->path.c_str(), iface::kProps, "PropertiesChanged",
                g_variant_new("(s@a{sv}@as)", iface::kGattChar, g_variant_builder_end(&b),
                              g_variant_new_strv(nullptr, 0)),
                &raw);
            if (raw) {
                ErrorPtr err(raw);
                return {Error::Failed, err->message};
            }
            return {};
        }
    }
    return {Error::NotFound, "local characteristic " + ch.toString()};
}

}  // namespace blemgr
