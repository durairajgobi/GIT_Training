// SPDX-License-Identifier: MIT
// Minimal stand-in for bluetoothd, used by the integration test on a private
// bus (DBUS_SYSTEM_BUS_ADDRESS). It models one adapter and one LE peripheral
// (11:22:33:44:55:66, "FakeSensor") exposing the Battery service, and
// exercises the client's agent, GATT application and advertisement objects
// the same way bluetoothd does (it calls back into them).
#include <gio/gio.h>

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

const char* kXml = R"XML(
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method>
  </interface>
  <interface name="org.bluez.AgentManager1">
    <method name="RegisterAgent"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
    <method name="UnregisterAgent"><arg type="o" direction="in"/></method>
    <method name="RequestDefaultAgent"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.bluez.Adapter1">
    <method name="StartDiscovery"/>
    <method name="StopDiscovery"/>
    <method name="SetDiscoveryFilter"><arg type="a{sv}" direction="in"/></method>
    <method name="RemoveDevice"><arg type="o" direction="in"/></method>
    <property name="Address" type="s" access="read"/>
    <property name="Alias" type="s" access="readwrite"/>
    <property name="Powered" type="b" access="readwrite"/>
    <property name="Discoverable" type="b" access="readwrite"/>
    <property name="DiscoverableTimeout" type="u" access="readwrite"/>
    <property name="Pairable" type="b" access="readwrite"/>
    <property name="PairableTimeout" type="u" access="readwrite"/>
    <property name="Discovering" type="b" access="read"/>
  </interface>
  <interface name="org.bluez.GattManager1">
    <method name="RegisterApplication"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/></method>
    <method name="UnregisterApplication"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.bluez.LEAdvertisingManager1">
    <method name="RegisterAdvertisement"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/></method>
    <method name="UnregisterAdvertisement"><arg type="o" direction="in"/></method>
  </interface>
  <interface name="org.bluez.Device1">
    <method name="Pair"/>
    <method name="CancelPairing"/>
    <method name="Connect"/>
    <method name="Disconnect"/>
    <property name="Address" type="s" access="read"/>
    <property name="AddressType" type="s" access="read"/>
    <property name="Name" type="s" access="read"/>
    <property name="Alias" type="s" access="read"/>
    <property name="RSSI" type="n" access="read"/>
    <property name="Paired" type="b" access="read"/>
    <property name="Trusted" type="b" access="readwrite"/>
    <property name="Connected" type="b" access="read"/>
    <property name="ServicesResolved" type="b" access="read"/>
    <property name="UUIDs" type="as" access="read"/>
  </interface>
  <interface name="org.bluez.GattService1">
    <property name="UUID" type="s" access="read"/>
    <property name="Device" type="o" access="read"/>
    <property name="Primary" type="b" access="read"/>
  </interface>
  <interface name="org.bluez.GattCharacteristic1">
    <method name="ReadValue"><arg type="a{sv}" direction="in"/><arg type="ay" direction="out"/></method>
    <method name="WriteValue"><arg type="ay" direction="in"/><arg type="a{sv}" direction="in"/></method>
    <method name="StartNotify"/>
    <method name="StopNotify"/>
    <property name="UUID" type="s" access="read"/>
    <property name="Service" type="o" access="read"/>
    <property name="Flags" type="as" access="read"/>
    <property name="Notifying" type="b" access="read"/>
  </interface>
</node>)XML";

const char* ADAPTER = "/org/bluez/hci0";
const char* DEVICE = "/org/bluez/hci0/dev_11_22_33_44_55_66";
const char* SERVICE = "/org/bluez/hci0/dev_11_22_33_44_55_66/service0010";
const char* CHAR = "/org/bluez/hci0/dev_11_22_33_44_55_66/service0010/char0011";
const char* BATT_SVC = "0000180f-0000-1000-8000-00805f9b34fb";
const char* BATT_LVL = "00002a19-0000-1000-8000-00805f9b34fb";

GDBusConnection* bus;
GDBusNodeInfo* info;
std::map<std::string, std::map<std::string, GVariant*>> props;  // "path|iface" -> name -> value
std::string agentOwner, agentPath;
bool deviceExported = false, gattExported = false;
std::vector<guint> deviceRegs, gattRegs;
std::vector<uint8_t> charValue = {0x55};

std::string key(const char* path, const char* iface) { return std::string(path) + "|" + iface; }

void put(const char* path, const char* iface, const char* name, GVariant* v) {
    auto& slot = props[key(path, iface)][name];
    if (slot) g_variant_unref(slot);
    slot = g_variant_ref_sink(v);
}

GVariant* propsDict(const char* path, const char* iface) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    for (auto& [n, v] : props[key(path, iface)]) g_variant_builder_add(&b, "{sv}", n.c_str(), v);
    return g_variant_builder_end(&b);
}

void changed(const char* path, const char* iface, const char* name, GVariant* v) {
    put(path, iface, name, v);
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", name, props[key(path, iface)][name]);
    g_dbus_connection_emit_signal(bus, nullptr, path, "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}@as)", iface, &b, g_variant_new_strv(nullptr, 0)),
                                  nullptr);
}

void interfacesAdded(const char* path, std::initializer_list<const char*> ifaces) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sa{sv}}"));
    for (const char* i : ifaces) g_variant_builder_add(&b, "{s@a{sv}}", i, propsDict(path, i));
    g_dbus_connection_emit_signal(bus, nullptr, "/", "org.freedesktop.DBus.ObjectManager",
                                  "InterfacesAdded", g_variant_new("(oa{sa{sv}})", path, &b), nullptr);
}

void method(GDBusConnection*, const gchar* sender, const gchar* path, const gchar* iface,
            const gchar* m, GVariant* params, GDBusMethodInvocation* inv, gpointer);
GVariant* getProp(GDBusConnection*, const gchar*, const gchar* path, const gchar* iface,
                  const gchar* name, GError** err, gpointer) {
    auto& p = props[key(path, iface)];
    auto it = p.find(name);
    if (it == p.end()) {
        g_set_error(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "%s", name);
        return nullptr;
    }
    return g_variant_ref(it->second);
}
gboolean setProp(GDBusConnection*, const gchar*, const gchar* path, const gchar* iface,
                 const gchar* name, GVariant* value, GError**, gpointer) {
    std::printf("fake-bluez: set %s.%s\n", path, name);
    std::fflush(stdout);
    changed(path, iface, name, value);
    return TRUE;
}
const GDBusInterfaceVTable vtable = {method, getProp, setProp, {}};

guint reg(const char* path, const char* iface) {
    GError* err = nullptr;
    guint id = g_dbus_connection_register_object(bus, path, g_dbus_node_info_lookup_interface(info, iface),
                                                 &vtable, nullptr, nullptr, &err);
    if (err) g_error("register %s %s: %s", path, iface, err->message);
    return id;
}

void exportDevice() {
    if (deviceExported) return;
    deviceExported = true;
    deviceRegs.push_back(reg(DEVICE, "org.bluez.Device1"));
    interfacesAdded(DEVICE, {"org.bluez.Device1"});
}

void exportGatt() {
    if (gattExported) return;
    gattExported = true;
    gattRegs.push_back(reg(SERVICE, "org.bluez.GattService1"));
    gattRegs.push_back(reg(CHAR, "org.bluez.GattCharacteristic1"));
    interfacesAdded(SERVICE, {"org.bluez.GattService1"});
    interfacesAdded(CHAR, {"org.bluez.GattCharacteristic1"});
}

void reply(GDBusMethodInvocation* inv) { g_dbus_method_invocation_return_value(inv, nullptr); }

void method(GDBusConnection*, const gchar* sender, const gchar*, const gchar* iface,
            const gchar* mc, GVariant* params, GDBusMethodInvocation* inv, gpointer) {
    std::string m = mc;
    std::printf("fake-bluez: %s.%s\n", iface, mc);
    std::fflush(stdout);

    if (m == "GetManagedObjects") {
        GVariantBuilder objs;
        g_variant_builder_init(&objs, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        auto add = [&](const char* p, const char* i) {
            GVariantBuilder ifs;
            g_variant_builder_init(&ifs, G_VARIANT_TYPE("a{sa{sv}}"));
            g_variant_builder_add(&ifs, "{s@a{sv}}", i, propsDict(p, i));
            g_variant_builder_add(&objs, "{oa{sa{sv}}}", p, &ifs);
        };
        add(ADAPTER, "org.bluez.Adapter1");
        if (deviceExported) add(DEVICE, "org.bluez.Device1");
        if (gattExported) { add(SERVICE, "org.bluez.GattService1"); add(CHAR, "org.bluez.GattCharacteristic1"); }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{oa{sa{sv}}})", &objs));
    } else if (m == "RegisterAgent") {
        const gchar *p, *cap;
        g_variant_get(params, "(&o&s)", &p, &cap);
        agentOwner = sender;
        agentPath = p;
        std::printf("fake-bluez: agent %s capability %s\n", p, cap);
        reply(inv);
    } else if (m == "StartDiscovery") {
        changed(ADAPTER, "org.bluez.Adapter1", "Discovering", g_variant_new_boolean(TRUE));
        reply(inv);
        g_timeout_add(200, [](gpointer) -> gboolean {
            if (deviceExported)  // cached device seen again: RSSI update, like bluetoothd
                changed(DEVICE, "org.bluez.Device1", "RSSI", g_variant_new_int16(-48));
            exportDevice();
            return G_SOURCE_REMOVE;
        }, nullptr);
    } else if (m == "StopDiscovery") {
        changed(ADAPTER, "org.bluez.Adapter1", "Discovering", g_variant_new_boolean(FALSE));
        reply(inv);
    } else if (m == "Pair") {
        // Ask the registered agent to confirm a passkey, as bluetoothd does.
        g_object_ref(inv);
        g_dbus_connection_call(bus, agentOwner.c_str(), agentPath.c_str(), "org.bluez.Agent1",
                               "RequestConfirmation", g_variant_new("(ou)", DEVICE, 123456u),
                               nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                               [](GObject* src, GAsyncResult* res, gpointer data) {
                                   auto* inv = static_cast<GDBusMethodInvocation*>(data);
                                   GError* err = nullptr;
                                   GVariant* r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
                                   if (err) {
                                       g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.AuthenticationRejected", err->message);
                                       g_error_free(err);
                                   } else {
                                       g_variant_unref(r);
                                       changed(DEVICE, "org.bluez.Device1", "Paired", g_variant_new_boolean(TRUE));
                                       g_dbus_method_invocation_return_value(inv, nullptr);
                                   }
                                   g_object_unref(inv);
                               }, inv);
    } else if (m == "Connect") {
        changed(DEVICE, "org.bluez.Device1", "Connected", g_variant_new_boolean(TRUE));
        reply(inv);
        g_timeout_add(100, [](gpointer) -> gboolean {
            exportGatt();
            changed(DEVICE, "org.bluez.Device1", "ServicesResolved", g_variant_new_boolean(TRUE));
            return G_SOURCE_REMOVE;
        }, nullptr);
    } else if (m == "Disconnect") {
        changed(DEVICE, "org.bluez.Device1", "ServicesResolved", g_variant_new_boolean(FALSE));
        changed(DEVICE, "org.bluez.Device1", "Connected", g_variant_new_boolean(FALSE));
        reply(inv);
    } else if (m == "ReadValue") {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)",
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, charValue.data(), charValue.size(), 1)));
    } else if (m == "WriteValue") {
        GVariant* ay = g_variant_get_child_value(params, 0);
        gsize n;
        auto* p = static_cast<const uint8_t*>(g_variant_get_fixed_array(ay, &n, 1));
        charValue.assign(p, p + n);
        g_variant_unref(ay);
        std::printf("fake-bluez: value now %zu byte(s)\n", charValue.size());
        reply(inv);
    } else if (m == "StartNotify") {
        changed(CHAR, "org.bluez.GattCharacteristic1", "Notifying", g_variant_new_boolean(TRUE));
        reply(inv);
        g_timeout_add(100, [](gpointer) -> gboolean {
            static uint8_t v = 90;
            uint8_t b = v--;
            GVariantBuilder d;
            g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&d, "{sv}", "Value", g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, &b, 1, 1));
            g_dbus_connection_emit_signal(bus, nullptr, CHAR, "org.freedesktop.DBus.Properties",
                                          "PropertiesChanged",
                                          g_variant_new("(sa{sv}@as)", "org.bluez.GattCharacteristic1", &d,
                                                        g_variant_new_strv(nullptr, 0)), nullptr);
            return v > 87 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
        }, nullptr);
    } else if (m == "RegisterApplication") {
        // bluetoothd reads the whole application tree through ObjectManager.
        const gchar* app;
        g_variant_get(params, "(&oa{sv})", &app, nullptr);
        g_object_ref(inv);
        g_dbus_connection_call(bus, sender, app, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                               nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                               [](GObject* src, GAsyncResult* res, gpointer data) {
                                   auto* inv = static_cast<GDBusMethodInvocation*>(data);
                                   GError* err = nullptr;
                                   GVariant* r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
                                   if (err) {
                                       g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Failed", err->message);
                                       g_error_free(err);
                                   } else {
                                       GVariant* objs = g_variant_get_child_value(r, 0);
                                       int svcs = 0, chrs = 0;
                                       GVariantIter it;
                                       const gchar* p;
                                       GVariant* ifs;
                                       g_variant_iter_init(&it, objs);
                                       while (g_variant_iter_next(&it, "{&o@a{sa{sv}}}", &p, &ifs)) {
                                           if (GVariant* s = g_variant_lookup_value(ifs, "org.bluez.GattService1", nullptr)) {
                                               ++svcs; g_variant_unref(s);
                                           }
                                           if (GVariant* c = g_variant_lookup_value(ifs, "org.bluez.GattCharacteristic1", nullptr)) {
                                               const gchar* uuid = nullptr;
                                               g_variant_lookup(c, "UUID", "&s", &uuid);
                                               std::printf("fake-bluez: app characteristic %s\n", uuid ? uuid : "?");
                                               ++chrs; g_variant_unref(c);
                                           }
                                           g_variant_unref(ifs);
                                       }
                                       std::printf("fake-bluez: application has %d service(s), %d characteristic(s)\n", svcs, chrs);
                                       std::fflush(stdout);
                                       g_variant_unref(objs);
                                       g_variant_unref(r);
                                       g_dbus_method_invocation_return_value(inv, nullptr);
                                   }
                                   g_object_unref(inv);
                               }, inv);
    } else if (m == "RegisterAdvertisement") {
        const gchar* adv;
        g_variant_get(params, "(&oa{sv})", &adv, nullptr);
        g_object_ref(inv);
        g_dbus_connection_call(bus, sender, adv, "org.freedesktop.DBus.Properties", "GetAll",
                               g_variant_new("(s)", "org.bluez.LEAdvertisement1"), G_VARIANT_TYPE("(a{sv})"),
                               G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                               [](GObject* src, GAsyncResult* res, gpointer data) {
                                   auto* inv = static_cast<GDBusMethodInvocation*>(data);
                                   GError* err = nullptr;
                                   GVariant* r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
                                   if (err) {
                                       g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Failed", err->message);
                                       g_error_free(err);
                                   } else {
                                       gchar* s = g_variant_print(r, FALSE);
                                       std::printf("fake-bluez: advertisement %s\n", s);
                                       std::fflush(stdout);
                                       g_free(s);
                                       g_variant_unref(r);
                                       g_dbus_method_invocation_return_value(inv, nullptr);
                                   }
                                   g_object_unref(inv);
                               }, inv);
    } else {
        reply(inv);  // UnregisterAgent, SetDiscoveryFilter, Unregister*, StopNotify, ...
    }
}

void initState() {
    const char* A = "org.bluez.Adapter1";
    put(ADAPTER, A, "Address", g_variant_new_string("00:AA:BB:CC:DD:EE"));
    put(ADAPTER, A, "Alias", g_variant_new_string("fake-hci0"));
    put(ADAPTER, A, "Powered", g_variant_new_boolean(FALSE));
    put(ADAPTER, A, "Discoverable", g_variant_new_boolean(FALSE));
    put(ADAPTER, A, "DiscoverableTimeout", g_variant_new_uint32(180));
    put(ADAPTER, A, "Pairable", g_variant_new_boolean(FALSE));
    put(ADAPTER, A, "PairableTimeout", g_variant_new_uint32(0));
    put(ADAPTER, A, "Discovering", g_variant_new_boolean(FALSE));
    const char* D = "org.bluez.Device1";
    put(DEVICE, D, "Address", g_variant_new_string("11:22:33:44:55:66"));
    put(DEVICE, D, "AddressType", g_variant_new_string("random"));
    put(DEVICE, D, "Name", g_variant_new_string("FakeSensor"));
    put(DEVICE, D, "Alias", g_variant_new_string("FakeSensor"));
    put(DEVICE, D, "RSSI", g_variant_new_int16(-52));
    put(DEVICE, D, "Paired", g_variant_new_boolean(FALSE));
    put(DEVICE, D, "Trusted", g_variant_new_boolean(FALSE));
    put(DEVICE, D, "Connected", g_variant_new_boolean(FALSE));
    put(DEVICE, D, "ServicesResolved", g_variant_new_boolean(FALSE));
    const gchar* uuids[] = {BATT_SVC, nullptr};
    put(DEVICE, D, "UUIDs", g_variant_new_strv(uuids, -1));
    put(SERVICE, "org.bluez.GattService1", "UUID", g_variant_new_string(BATT_SVC));
    put(SERVICE, "org.bluez.GattService1", "Device", g_variant_new_object_path(DEVICE));
    put(SERVICE, "org.bluez.GattService1", "Primary", g_variant_new_boolean(TRUE));
    const char* C = "org.bluez.GattCharacteristic1";
    put(CHAR, C, "UUID", g_variant_new_string(BATT_LVL));
    put(CHAR, C, "Service", g_variant_new_object_path(SERVICE));
    const gchar* flags[] = {"read", "write", "notify", nullptr};
    put(CHAR, C, "Flags", g_variant_new_strv(flags, -1));
    put(CHAR, C, "Notifying", g_variant_new_boolean(FALSE));
}

}  // namespace

int main() {
    GError* err = nullptr;
    bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (err) g_error("bus: %s", err->message);
    info = g_dbus_node_info_new_for_xml(kXml, &err);
    if (err) g_error("xml: %s", err->message);
    initState();

    reg("/", "org.freedesktop.DBus.ObjectManager");
    reg("/org/bluez", "org.bluez.AgentManager1");
    reg(ADAPTER, "org.bluez.Adapter1");
    reg(ADAPTER, "org.bluez.GattManager1");
    reg(ADAPTER, "org.bluez.LEAdvertisingManager1");

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_bus_own_name_on_connection(bus, "org.bluez", G_BUS_NAME_OWNER_FLAGS_NONE,
                                 [](GDBusConnection*, const gchar*, gpointer) {
                                     std::printf("fake-bluez: ready\n");
                                     std::fflush(stdout);
                                 },
                                 [](GDBusConnection*, const gchar*, gpointer l) {
                                     g_main_loop_quit(static_cast<GMainLoop*>(l));
                                 },
                                 loop, nullptr);
    g_main_loop_run(loop);
    return 0;
}
