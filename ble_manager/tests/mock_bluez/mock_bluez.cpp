// SPDX-License-Identifier: MIT
// mock_bluez - a tiny stand-in for bluetoothd used by the end-to-end test.
//
//   mock_bluez <dbus-address>
//
// Owns "org.bluez" on the given bus and implements the subset of the BlueZ
// D-Bus API that blemgr uses: ObjectManager, Adapter1, Device1,
// GattService1/GattCharacteristic1, AgentManager1, LEAdvertisingManager1 and
// GattManager1. One LE peripheral (heart-rate like) "appears" once discovery
// starts. A control object (/mock, com.tcu.MockControl) lets the test inject
// link loss and inspect what the client did.
#include <gio/gio.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kAdapter = "/org/bluez/hci0";
constexpr const char* kDev = "/org/bluez/hci0/dev_11_22_33_44_55_66";
constexpr const char* kSvc = "/org/bluez/hci0/dev_11_22_33_44_55_66/service0001";
constexpr const char* kHrmChr = "/org/bluez/hci0/dev_11_22_33_44_55_66/service0001/char0002";
constexpr const char* kCtlChr = "/org/bluez/hci0/dev_11_22_33_44_55_66/service0001/char0004";

constexpr const char* kXml = R"XML(
<node>
  <interface name='org.freedesktop.DBus.ObjectManager'>
    <method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method>
  </interface>
  <interface name='org.bluez.AgentManager1'>
    <method name='RegisterAgent'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
    <method name='RequestDefaultAgent'><arg type='o' direction='in'/></method>
    <method name='UnregisterAgent'><arg type='o' direction='in'/></method>
  </interface>
  <interface name='org.bluez.Adapter1'>
    <method name='StartDiscovery'/>
    <method name='StopDiscovery'/>
    <method name='SetDiscoveryFilter'><arg type='a{sv}' direction='in'/></method>
    <method name='RemoveDevice'><arg type='o' direction='in'/></method>
    <property name='Address' type='s' access='read'/>
    <property name='Powered' type='b' access='readwrite'/>
    <property name='Discovering' type='b' access='read'/>
  </interface>
  <interface name='org.bluez.LEAdvertisingManager1'>
    <method name='RegisterAdvertisement'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>
    <method name='UnregisterAdvertisement'><arg type='o' direction='in'/></method>
  </interface>
  <interface name='org.bluez.GattManager1'>
    <method name='RegisterApplication'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>
    <method name='UnregisterApplication'><arg type='o' direction='in'/></method>
  </interface>
  <interface name='org.bluez.Device1'>
    <method name='Connect'/>
    <method name='Disconnect'/>
    <method name='Pair'/>
    <property name='Address' type='s' access='read'/>
    <property name='AddressType' type='s' access='read'/>
    <property name='Name' type='s' access='read'/>
    <property name='RSSI' type='n' access='read'/>
    <property name='UUIDs' type='as' access='read'/>
    <property name='ManufacturerData' type='a{qv}' access='read'/>
    <property name='Paired' type='b' access='read'/>
    <property name='Trusted' type='b' access='readwrite'/>
    <property name='Connected' type='b' access='read'/>
    <property name='ServicesResolved' type='b' access='read'/>
  </interface>
  <interface name='org.bluez.GattService1'>
    <property name='UUID' type='s' access='read'/>
    <property name='Primary' type='b' access='read'/>
    <property name='Device' type='o' access='read'/>
  </interface>
  <interface name='org.bluez.GattCharacteristic1'>
    <method name='ReadValue'><arg type='a{sv}' direction='in'/><arg type='ay' direction='out'/></method>
    <method name='WriteValue'><arg type='ay' direction='in'/><arg type='a{sv}' direction='in'/></method>
    <method name='StartNotify'/>
    <method name='StopNotify'/>
    <property name='UUID' type='s' access='read'/>
    <property name='Service' type='o' access='read'/>
    <property name='Flags' type='as' access='read'/>
    <property name='Value' type='ay' access='read'/>
    <property name='Notifying' type='b' access='read'/>
  </interface>
  <interface name='com.tcu.MockControl'>
    <method name='DropLink'/>
    <method name='GetReport'><arg type='a{ss}' direction='out'/></method>
  </interface>
</node>)XML";

GDBusConnection* g_conn;
GDBusNodeInfo* g_node;

struct Object {
    // iface -> property -> value
    std::map<std::string, std::map<std::string, GVariant*>> props;
    std::vector<guint> regs;
};
std::map<std::string, Object> g_objects;
std::map<std::string, std::string> g_report;
guint g_notifyTimer = 0;
guint8 g_counter = 0;
bool g_discovering = false;

GVariant* bytes(std::initializer_list<guint8> b) {
    std::vector<guint8> v(b);
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, v.data(), v.size(), 1);
}

std::string hex(GVariant* ay) {
    gsize n = 0;
    auto* p = static_cast<const guint8*>(g_variant_get_fixed_array(ay, &n, 1));
    std::string s;
    char buf[3];
    for (gsize i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof buf, "%02x", p[i]);
        s += buf;
    }
    return s;
}

GVariant* ifaceDict(const Object& o) {
    GVariantBuilder ifs;
    g_variant_builder_init(&ifs, G_VARIANT_TYPE("a{sa{sv}}"));
    for (const auto& [ifc, props] : o.props) {
        if (ifc == "org.freedesktop.DBus.ObjectManager" || ifc == "com.tcu.MockControl") continue;
        GVariantBuilder pb;
        g_variant_builder_init(&pb, G_VARIANT_TYPE("a{sv}"));
        for (const auto& [k, v] : props) g_variant_builder_add(&pb, "{sv}", k.c_str(), v);
        g_variant_builder_add(&ifs, "{sa{sv}}", ifc.c_str(), &pb);
    }
    return g_variant_builder_end(&ifs);
}

void setProp(const std::string& path, const std::string& ifc, const std::string& name, GVariant* v,
             bool emit = true) {
    auto& slot = g_objects[path].props[ifc][name];
    if (slot) g_variant_unref(slot);
    slot = g_variant_ref_sink(v);
    if (!emit) return;
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", name.c_str(), slot);
    g_dbus_connection_emit_signal(g_conn, nullptr, path.c_str(), "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}@as)", ifc.c_str(), &b,
                                                g_variant_new_strv(nullptr, 0)),
                                  nullptr);
}

bool getBool(const std::string& path, const char* ifc, const char* name) {
    GVariant* v = g_objects[path].props[ifc][name];
    return v && g_variant_get_boolean(v);
}

void methodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                GVariant*, GDBusMethodInvocation*, gpointer);
GVariant* getProperty(GDBusConnection*, const gchar*, const gchar* path, const gchar* ifc,
                      const gchar* name, GError** err, gpointer) {
    auto& p = g_objects[path].props[ifc];
    auto it = p.find(name);
    if (it == p.end() || !it->second) {
        g_set_error(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "%s", name);
        return nullptr;
    }
    return g_variant_ref(it->second);
}
gboolean setProperty(GDBusConnection*, const gchar*, const gchar* path, const gchar* ifc,
                     const gchar* name, GVariant* value, GError**, gpointer) {
    setProp(path, ifc, name, value);
    return TRUE;
}

void addObject(const std::string& path, std::map<std::string, std::map<std::string, GVariant*>> ifaces,
               bool announce = true) {
    Object& o = g_objects[path];
    GDBusInterfaceVTable vt{};
    vt.method_call = methodCall;
    vt.get_property = getProperty;
    vt.set_property = setProperty;
    for (auto& [ifc, props] : ifaces) {
        for (auto& [k, v] : props) o.props[ifc][k] = g_variant_ref_sink(v);
        if (props.empty()) o.props[ifc];
        GDBusInterfaceInfo* info = g_dbus_node_info_lookup_interface(g_node, ifc.c_str());
        o.regs.push_back(
            g_dbus_connection_register_object(g_conn, path.c_str(), info, &vt, nullptr, nullptr, nullptr));
    }
    if (announce)
        g_dbus_connection_emit_signal(g_conn, nullptr, "/", "org.freedesktop.DBus.ObjectManager",
                                      "InterfacesAdded",
                                      g_variant_new("(o@a{sa{sv}})", path.c_str(), ifaceDict(o)), nullptr);
}

void removeObject(const std::string& path) {
    auto it = g_objects.find(path);
    if (it == g_objects.end()) return;
    std::vector<const gchar*> names;
    for (auto& [ifc, props] : it->second.props) names.push_back(ifc.c_str());
    for (guint r : it->second.regs) g_dbus_connection_unregister_object(g_conn, r);
    g_dbus_connection_emit_signal(
        g_conn, nullptr, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved",
        g_variant_new("(o@as)", path.c_str(), g_variant_new_strv(names.data(), names.size())), nullptr);
    for (auto& [ifc, props] : it->second.props)
        for (auto& [k, v] : props)
            if (v) g_variant_unref(v);
    g_objects.erase(it);
}

const char* hrUuid(const char* s) {
    static std::map<std::string, std::string> cache;
    auto& v = cache[s];
    v = std::string("0000") + s + "-0000-1000-8000-00805f9b34fb";
    return v.c_str();
}

void addDevice() {
    if (g_objects.count(kDev)) return;
    const gchar* uuids[] = {hrUuid("180d"), nullptr};
    GVariantBuilder md;
    g_variant_builder_init(&md, G_VARIANT_TYPE("a{qv}"));
    g_variant_builder_add(&md, "{qv}", static_cast<guint16>(0xffff), bytes({0xca, 0xfe}));
    addObject(kDev, {{"org.bluez.Device1",
                      {{"Address", g_variant_new_string("11:22:33:44:55:66")},
                       {"AddressType", g_variant_new_string("random")},
                       {"Name", g_variant_new_string("Mock-HRM")},
                       {"RSSI", g_variant_new_int16(-55)},
                       {"UUIDs", g_variant_new_strv(uuids, -1)},
                       {"ManufacturerData", g_variant_builder_end(&md)},
                       {"Paired", g_variant_new_boolean(FALSE)},
                       {"Trusted", g_variant_new_boolean(FALSE)},
                       {"Connected", g_variant_new_boolean(FALSE)},
                       {"ServicesResolved", g_variant_new_boolean(FALSE)}}}});
}

void stopNotify() {
    if (g_notifyTimer) g_source_remove(g_notifyTimer);
    g_notifyTimer = 0;
}

void addGatt() {
    const gchar* hrFlags[] = {"read", "notify", nullptr};
    const gchar* ctlFlags[] = {"write", nullptr};
    addObject(kSvc, {{"org.bluez.GattService1",
                      {{"UUID", g_variant_new_string(hrUuid("180d"))},
                       {"Primary", g_variant_new_boolean(TRUE)},
                       {"Device", g_variant_new_object_path(kDev)}}}});
    addObject(kHrmChr, {{"org.bluez.GattCharacteristic1",
                         {{"UUID", g_variant_new_string(hrUuid("2a37"))},
                          {"Service", g_variant_new_object_path(kSvc)},
                          {"Flags", g_variant_new_strv(hrFlags, -1)},
                          {"Value", bytes({})},
                          {"Notifying", g_variant_new_boolean(FALSE)}}}});
    addObject(kCtlChr, {{"org.bluez.GattCharacteristic1",
                         {{"UUID", g_variant_new_string(hrUuid("2a39"))},
                          {"Service", g_variant_new_object_path(kSvc)},
                          {"Flags", g_variant_new_strv(ctlFlags, -1)},
                          {"Value", bytes({})},
                          {"Notifying", g_variant_new_boolean(FALSE)}}}});
}

void linkDown() {
    stopNotify();
    removeObject(kCtlChr);
    removeObject(kHrmChr);
    removeObject(kSvc);
    setProp(kDev, "org.bluez.Device1", "ServicesResolved", g_variant_new_boolean(FALSE));
    setProp(kDev, "org.bluez.Device1", "Connected", g_variant_new_boolean(FALSE));
}

void reply(GDBusMethodInvocation* inv) { g_dbus_method_invocation_return_value(inv, nullptr); }
void fail(GDBusMethodInvocation* inv, const char* name, const char* msg) {
    g_dbus_method_invocation_return_dbus_error(inv, name, msg);
}

// Calls back into the client's exported GATT application / advertisement, like bluetoothd does.
void probeApplication(const std::string& sender, const std::string& root, GDBusMethodInvocation* inv) {
    struct Ctx {
        std::string sender;
        GDBusMethodInvocation* inv;
    };
    g_dbus_connection_call(
        g_conn, sender.c_str(), root.c_str(), "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
        nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer ud) {
            std::unique_ptr<Ctx> c(static_cast<Ctx*>(ud));
            GError* err = nullptr;
            GVariant* r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
            if (!r) {
                fail(c->inv, "org.bluez.Error.Failed", err->message);
                g_error_free(err);
                return;
            }
            GVariant* objs = g_variant_get_child_value(r, 0);
            g_report["app_objects"] = std::to_string(g_variant_n_children(objs));
            std::string readPath, writePath;
            GVariantIter it;
            g_variant_iter_init(&it, objs);
            const gchar* path;
            GVariant* ifaces;
            while (g_variant_iter_loop(&it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
                GVariant* ch = g_variant_lookup_value(ifaces, "org.bluez.GattCharacteristic1", nullptr);
                if (!ch) continue;
                const gchar* uuid = nullptr;
                g_variant_lookup(ch, "UUID", "&s", &uuid);
                if (uuid && std::strcmp(uuid, hrUuid("2a29")) == 0) readPath = path;
                if (uuid && std::strncmp(uuid, "a0b40003", 8) == 0) writePath = path;
                g_variant_unref(ch);
            }
            g_variant_unref(objs);
            g_variant_unref(r);
            reply(c->inv);  // registration done; now act like a remote central

            if (!readPath.empty()) {
                GVariant* rv = g_dbus_connection_call_sync(
                    g_conn, c->sender.c_str(), readPath.c_str(), "org.bluez.GattCharacteristic1",
                    "ReadValue", g_variant_new("(@a{sv})", g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)),
                    nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
                if (rv) {
                    GVariant* ay = g_variant_get_child_value(rv, 0);
                    g_report["app_read"] = hex(ay);
                    g_variant_unref(ay);
                    g_variant_unref(rv);
                }
            }
            if (!writePath.empty()) {
                GError* werr = nullptr;
                GVariant* wv = g_dbus_connection_call_sync(
                    g_conn, c->sender.c_str(), writePath.c_str(), "org.bluez.GattCharacteristic1",
                    "WriteValue",
                    g_variant_new("(@ay@a{sv})", bytes({0x01, 0x02}),
                                  g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)),
                    nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &werr);
                g_report["app_write"] = wv ? "ok" : werr->message;
                if (wv) g_variant_unref(wv);
                if (werr) g_error_free(werr);
            }
        },
        new Ctx{sender, inv});
}

void methodCall(GDBusConnection*, const gchar* sender, const gchar* path, const gchar* ifc,
                const gchar* method, GVariant* params, GDBusMethodInvocation* inv, gpointer) {
    const std::string i = ifc, m = method, p = path;

    if (i == "org.freedesktop.DBus.ObjectManager") {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        for (const auto& [op, o] : g_objects) {
            if (op == "/" || op == "/mock") continue;
            g_variant_builder_add(&b, "{o@a{sa{sv}}}", op.c_str(), ifaceDict(o));
        }
        return g_dbus_method_invocation_return_value(inv, g_variant_new("(a{oa{sa{sv}}})", &b));
    }
    if (i == "org.bluez.AgentManager1") {
        if (m == "RegisterAgent") {
            const gchar* cap = nullptr;
            g_variant_get(params, "(&o&s)", nullptr, &cap);
            g_report["agent"] = cap;
        } else if (m == "RequestDefaultAgent") {
            g_report["default_agent"] = "yes";
        }
        return reply(inv);
    }
    if (i == "org.bluez.Adapter1") {
        if (!getBool(kAdapter, "org.bluez.Adapter1", "Powered"))
            return fail(inv, "org.bluez.Error.NotReady", "Resource Not Ready");
        if (m == "SetDiscoveryFilter") {
            GVariant* f = g_variant_get_child_value(params, 0);
            const gchar* transport = nullptr;
            if (g_variant_lookup(f, "Transport", "&s", &transport)) g_report["filter_transport"] = transport;
            g_variant_unref(f);
            return reply(inv);
        }
        if (m == "StartDiscovery") {
            if (g_discovering) return fail(inv, "org.bluez.Error.InProgress", "Operation already in progress");
            g_discovering = true;
            setProp(kAdapter, "org.bluez.Adapter1", "Discovering", g_variant_new_boolean(TRUE));
            g_timeout_add(100, [](gpointer) -> gboolean {
                if (g_discovering) addDevice();
                return G_SOURCE_REMOVE;
            }, nullptr);
            return reply(inv);
        }
        if (m == "StopDiscovery") {
            if (!g_discovering) return fail(inv, "org.bluez.Error.Failed", "No discovery started");
            g_discovering = false;
            setProp(kAdapter, "org.bluez.Adapter1", "Discovering", g_variant_new_boolean(FALSE));
            return reply(inv);
        }
        if (m == "RemoveDevice") {
            linkDown();
            removeObject(kDev);
            return reply(inv);
        }
    }
    if (i == "org.bluez.LEAdvertisingManager1") {
        if (m == "UnregisterAdvertisement") return reply(inv);
        const gchar* advPath = nullptr;
        g_variant_get(params, "(&o@a{sv})", &advPath, nullptr);
        GError* err = nullptr;
        GVariant* all = g_dbus_connection_call_sync(
            g_conn, sender, advPath, "org.freedesktop.DBus.Properties", "GetAll",
            g_variant_new("(s)", "org.bluez.LEAdvertisement1"), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000,
            nullptr, &err);
        if (!all) {
            fail(inv, "org.bluez.Error.Failed", err->message);
            return g_error_free(err);
        }
        GVariant* props = g_variant_get_child_value(all, 0);
        const gchar* name = nullptr;
        const gchar* type = nullptr;
        if (g_variant_lookup(props, "LocalName", "&s", &name)) g_report["adv_name"] = name;
        if (g_variant_lookup(props, "Type", "&s", &type)) g_report["adv_type"] = type;
        g_variant_unref(props);
        g_variant_unref(all);
        return reply(inv);
    }
    if (i == "org.bluez.GattManager1") {
        if (m == "UnregisterApplication") return reply(inv);
        const gchar* root = nullptr;
        g_variant_get(params, "(&o@a{sv})", &root, nullptr);
        return probeApplication(sender, root, inv);
    }
    if (i == "org.bluez.Device1") {
        if (m == "Connect") {
            if (getBool(kDev, "org.bluez.Device1", "Connected"))
                return fail(inv, "org.bluez.Error.AlreadyConnected", "Already Connected");
            g_report["connects"] = std::to_string(std::stoi(g_report["connects"].empty() ? "0" : g_report["connects"]) + 1);
            setProp(kDev, "org.bluez.Device1", "Connected", g_variant_new_boolean(TRUE));
            reply(inv);
            g_timeout_add(50, [](gpointer) -> gboolean {
                if (!getBool(kDev, "org.bluez.Device1", "Connected")) return G_SOURCE_REMOVE;
                addGatt();
                setProp(kDev, "org.bluez.Device1", "ServicesResolved", g_variant_new_boolean(TRUE));
                return G_SOURCE_REMOVE;
            }, nullptr);
            return;
        }
        if (m == "Disconnect") {
            linkDown();
            return reply(inv);
        }
        if (m == "Pair") {
            setProp(kDev, "org.bluez.Device1", "Paired", g_variant_new_boolean(TRUE));
            return reply(inv);
        }
    }
    if (i == "org.bluez.GattCharacteristic1") {
        if (m == "ReadValue")
            return g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", bytes({0x06, 0x48})));
        if (m == "WriteValue") {
            GVariant* v = g_variant_get_child_value(params, 0);
            GVariant* o = g_variant_get_child_value(params, 1);
            const gchar* type = nullptr;
            g_report["last_write"] = hex(v);
            if (g_variant_lookup(o, "type", "&s", &type)) g_report["last_write_type"] = type;
            g_variant_unref(v);
            g_variant_unref(o);
            return reply(inv);
        }
        if (m == "StartNotify") {
            if (getBool(p, "org.bluez.GattCharacteristic1", "Notifying"))
                return fail(inv, "org.bluez.Error.InProgress", "Already notifying");
            setProp(p, "org.bluez.GattCharacteristic1", "Notifying", g_variant_new_boolean(TRUE));
            stopNotify();
            g_notifyTimer = g_timeout_add(100, [](gpointer) -> gboolean {
                setProp(kHrmChr, "org.bluez.GattCharacteristic1", "Value", bytes({0x06, ++g_counter}));
                return G_SOURCE_CONTINUE;
            }, nullptr);
            return reply(inv);
        }
        if (m == "StopNotify") {
            stopNotify();
            setProp(p, "org.bluez.GattCharacteristic1", "Notifying", g_variant_new_boolean(FALSE));
            return reply(inv);
        }
    }
    if (i == "com.tcu.MockControl") {
        if (m == "DropLink") {
            linkDown();
            return reply(inv);
        }
        if (m == "GetReport") {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("a{ss}"));
            for (const auto& [k, v] : g_report) g_variant_builder_add(&b, "{ss}", k.c_str(), v.c_str());
            return g_dbus_method_invocation_return_value(inv, g_variant_new("(a{ss})", &b));
        }
    }
    fail(inv, "org.freedesktop.DBus.Error.UnknownMethod", method);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: mock_bluez <dbus-address>\n");
        return 2;
    }
    GError* err = nullptr;
    g_conn = g_dbus_connection_new_for_address_sync(
        argv[1],
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &err);
    if (!g_conn) {
        std::fprintf(stderr, "mock_bluez: %s\n", err->message);
        return 1;
    }
    g_node = g_dbus_node_info_new_for_xml(kXml, &err);
    if (!g_node) {
        std::fprintf(stderr, "mock_bluez: %s\n", err->message);
        return 1;
    }

    addObject("/", {{"org.freedesktop.DBus.ObjectManager", {}}}, false);
    addObject("/mock", {{"com.tcu.MockControl", {}}}, false);
    addObject("/org/bluez", {{"org.bluez.AgentManager1", {}}}, false);
    addObject(kAdapter, {{"org.bluez.Adapter1",
                          {{"Address", g_variant_new_string("00:1A:7D:DA:71:13")},
                           {"Powered", g_variant_new_boolean(FALSE)},
                           {"Discovering", g_variant_new_boolean(FALSE)}}},
                         {"org.bluez.LEAdvertisingManager1", {}},
                         {"org.bluez.GattManager1", {}}},
              false);

    GVariant* r = g_dbus_connection_call_sync(
        g_conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", "org.bluez", 4u /* DO_NOT_QUEUE */),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
    if (!r) {
        std::fprintf(stderr, "mock_bluez: RequestName: %s\n", err->message);
        return 1;
    }
    g_variant_unref(r);

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);
    return 0;
}
