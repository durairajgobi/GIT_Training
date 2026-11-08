// SPDX-License-Identifier: MIT
#include "ble/agent.hpp"

#include <random>

#include "ble/dbus.hpp"

namespace ble {
namespace {

const char* kAgentXml = R"XML(
<node>
  <interface name="org.bluez.Agent1">
    <method name="Release"/>
    <method name="RequestPinCode">
      <arg type="o" name="device" direction="in"/>
      <arg type="s" name="pincode" direction="out"/>
    </method>
    <method name="DisplayPinCode">
      <arg type="o" name="device" direction="in"/>
      <arg type="s" name="pincode" direction="in"/>
    </method>
    <method name="RequestPasskey">
      <arg type="o" name="device" direction="in"/>
      <arg type="u" name="passkey" direction="out"/>
    </method>
    <method name="DisplayPasskey">
      <arg type="o" name="device" direction="in"/>
      <arg type="u" name="passkey" direction="in"/>
      <arg type="q" name="entered" direction="in"/>
    </method>
    <method name="RequestConfirmation">
      <arg type="o" name="device" direction="in"/>
      <arg type="u" name="passkey" direction="in"/>
    </method>
    <method name="RequestAuthorization">
      <arg type="o" name="device" direction="in"/>
    </method>
    <method name="AuthorizeService">
      <arg type="o" name="device" direction="in"/>
      <arg type="s" name="uuid" direction="in"/>
    </method>
    <method name="Cancel"/>
  </interface>
</node>)XML";

void reject(GDBusMethodInvocation* inv, const std::string& why) {
    g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Rejected", why.c_str());
}

}  // namespace

Agent::Agent(GDBusConnection* conn, Capability cap, std::string path)
    : conn_(conn), cap_(cap), path_(std::move(path)) {}

Agent::~Agent() {
    try {
        unregisterAgent();
    } catch (...) {
    }
}

const char* Agent::capabilityString(Capability cap) {
    switch (cap) {
        case Capability::DisplayOnly: return "DisplayOnly";
        case Capability::DisplayYesNo: return "DisplayYesNo";
        case Capability::KeyboardOnly: return "KeyboardOnly";
        case Capability::NoInputNoOutput: return "NoInputNoOutput";
        case Capability::KeyboardDisplay: return "KeyboardDisplay";
    }
    return "KeyboardDisplay";
}

void Agent::registerAgent(bool makeDefault) {
    if (!regId_) {
        GError* err = nullptr;
        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kAgentXml, &err);
        throwIfError(err, "parsing agent introspection");
        static const GDBusInterfaceVTable vtable = {handleMethod, nullptr, nullptr, {}};
        regId_ = g_dbus_connection_register_object(conn_, path_.c_str(), node->interfaces[0],
                                                   &vtable, this, nullptr, &err);
        g_dbus_node_info_unref(node);
        throwIfError(err, "exporting agent object");
    }
    callSync(conn_, "/org/bluez", iface::kAgentManager, "RegisterAgent",
             g_variant_new("(os)", path_.c_str(), capabilityString(cap_)));
    registered_ = true;
    if (makeDefault)
        callSync(conn_, "/org/bluez", iface::kAgentManager, "RequestDefaultAgent",
                 g_variant_new("(o)", path_.c_str()));
}

void Agent::unregisterAgent() {
    if (registered_) {
        registered_ = false;
        callSync(conn_, "/org/bluez", iface::kAgentManager, "UnregisterAgent",
                 g_variant_new("(o)", path_.c_str()));
    }
    if (regId_) {
        g_dbus_connection_unregister_object(conn_, regId_);
        regId_ = 0;
    }
}

void Agent::handleMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                         const gchar* method, GVariant* params, GDBusMethodInvocation* inv,
                         gpointer self) {
    try {
        static_cast<Agent*>(self)->dispatch(method, params, inv);
    } catch (const std::exception& e) {
        reject(inv, e.what());
    }
}

void Agent::dispatch(const std::string& method, GVariant* params, GDBusMethodInvocation* inv) {
    const gchar* dev = nullptr;

    if (method == "Release") {
        registered_ = false;
        g_message("agent released by bluetoothd");
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (method == "Cancel") {
        g_message("agent request cancelled");
        if (onCancel) onCancel();
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (method == "RequestPinCode") {
        g_variant_get(params, "(&o)", &dev);
        std::string pin = onRequestPinCode ? onRequestPinCode(dev) : "0000";
        g_message("[%s] PIN code requested -> %s", dev, pin.c_str());
        g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", pin.c_str()));
    } else if (method == "DisplayPinCode") {
        const gchar* pin;
        g_variant_get(params, "(&o&s)", &dev, &pin);
        g_message("[%s] PIN code: %s", dev, pin);
        if (onDisplayPinCode) onDisplayPinCode(dev, pin);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (method == "RequestPasskey") {
        g_variant_get(params, "(&o)", &dev);
        uint32_t passkey;
        if (onRequestPasskey) {
            passkey = onRequestPasskey(dev);
        } else {
            std::random_device rd;
            passkey = std::uniform_int_distribution<uint32_t>(0, 999999)(rd);
        }
        g_message("[%s] passkey requested -> %06u", dev, passkey);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", passkey));
    } else if (method == "DisplayPasskey") {
        guint32 passkey;
        guint16 entered;
        g_variant_get(params, "(&ouq)", &dev, &passkey, &entered);
        g_message("[%s] passkey: %06u (entered %u)", dev, passkey, entered);
        if (onDisplayPasskey) onDisplayPasskey(dev, passkey, entered);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (method == "RequestConfirmation") {
        guint32 passkey;
        g_variant_get(params, "(&ou)", &dev, &passkey);
        g_message("[%s] confirm passkey %06u", dev, passkey);
        if (onConfirm && !onConfirm(dev, passkey)) return reject(inv, "passkey rejected");
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (method == "RequestAuthorization") {
        g_variant_get(params, "(&o)", &dev);
        g_message("[%s] authorization requested", dev);
        if (onAuthorize && !onAuthorize(dev)) return reject(inv, "pairing not authorized");
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (method == "AuthorizeService") {
        const gchar* uuid;
        g_variant_get(params, "(&o&s)", &dev, &uuid);
        g_message("[%s] authorize service %s", dev, uuid);
        if (onAuthorizeService && !onAuthorizeService(dev, uuid))
            return reject(inv, "service not authorized");
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod",
                                                   method.c_str());
    }
}

}  // namespace ble
