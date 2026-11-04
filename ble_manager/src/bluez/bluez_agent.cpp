// SPDX-License-Identifier: MIT
// org.bluez.Agent1 implementation: answers pairing requests according to the
// configured IO capability and policy (headless units: NoInputNoOutput).
#include "blemgr/log.hpp"
#include "bluez_impl.hpp"

namespace blemgr {

namespace {

constexpr const char* kAgentXml = R"XML(
<node>
  <interface name='org.bluez.Agent1'>
    <method name='Release'/>
    <method name='RequestPinCode'>
      <arg type='o' direction='in'/><arg type='s' direction='out'/>
    </method>
    <method name='DisplayPinCode'>
      <arg type='o' direction='in'/><arg type='s' direction='in'/>
    </method>
    <method name='RequestPasskey'>
      <arg type='o' direction='in'/><arg type='u' direction='out'/>
    </method>
    <method name='DisplayPasskey'>
      <arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/>
    </method>
    <method name='RequestConfirmation'>
      <arg type='o' direction='in'/><arg type='u' direction='in'/>
    </method>
    <method name='RequestAuthorization'>
      <arg type='o' direction='in'/>
    </method>
    <method name='AuthorizeService'>
      <arg type='o' direction='in'/><arg type='s' direction='in'/>
    </method>
    <method name='Cancel'/>
  </interface>
</node>)XML";

std::string agentPath(const BluezOptions& o) { return o.objectRoot + "/agent"; }

std::string who(GVariant* params) {
    if (g_variant_n_children(params) == 0) return "?";
    gdbus::VariantPtr p(g_variant_get_child_value(params, 0));
    if (!g_variant_is_of_type(p.get(), G_VARIANT_TYPE_OBJECT_PATH)) return "?";
    std::string path = g_variant_get_string(p.get(), nullptr);
    return gdbus::addressFromPath(path).value_or(path);
}

}  // namespace

void BluezBackend::Impl::registerAgent() {
    if (opts.agentCapability.empty() || !conn) return;
    const std::string path = agentPath(opts);
    if (!agentReg) {
        static GDBusNodeInfo* node = parseNodeXml(kAgentXml);  // process lifetime
        GDBusInterfaceVTable vt{};
        vt.method_call = &Impl::agentMethod;
        GError* raw = nullptr;
        agentReg = g_dbus_connection_register_object(conn, path.c_str(), node->interfaces[0], &vt,
                                                     this, nullptr, &raw);
        if (!agentReg) {
            gdbus::ErrorPtr err(raw);
            BLE_LOGE("agent export failed: %s", err->message);
            return;
        }
    }
    call("/org/bluez", iface::kAgentManager, "RegisterAgent",
         g_variant_new("(os)", path.c_str(), opts.agentCapability.c_str()),
         [this, path](GVariant*, const Status& s) {
             if (!s.ok()) {
                 BLE_LOGW("RegisterAgent: %s", s.toString().c_str());
                 return;
             }
             call("/org/bluez", iface::kAgentManager, "RequestDefaultAgent",
                  g_variant_new("(o)", path.c_str()), [this](GVariant*, const Status& st) {
                      if (st.ok())
                          BLE_LOGI("pairing agent registered (%s)", opts.agentCapability.c_str());
                      else
                          BLE_LOGW("RequestDefaultAgent: %s", st.toString().c_str());
                  });
         });
}

void BluezBackend::Impl::unregisterAgent() {
    if (!agentReg) return;
    call("/org/bluez", iface::kAgentManager, "UnregisterAgent",
         g_variant_new("(o)", agentPath(opts).c_str()), [](GVariant*, const Status&) {});
    g_dbus_connection_unregister_object(conn, agentReg);
    agentReg = 0;
}

void BluezBackend::Impl::agentMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                     const gchar* method, GVariant* params,
                                     GDBusMethodInvocation* inv, gpointer self) {
    auto* d = static_cast<Impl*>(self);
    const std::string m = method;
    const std::string dev = who(params);
    auto reject = [&](const char* why) {
        BLE_LOGW("agent: %s from %s rejected (%s)", method, dev.c_str(), why);
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Rejected", why);
    };

    if (m == "Release" || m == "Cancel") {
        BLE_LOGI("agent: %s", method);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (m == "RequestPinCode") {
        BLE_LOGI("agent: PIN code requested by %s", dev.c_str());
        g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", d->opts.agentPinCode.c_str()));
    } else if (m == "RequestPasskey") {
        BLE_LOGI("agent: passkey requested by %s", dev.c_str());
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", d->opts.agentPasskey));
    } else if (m == "DisplayPinCode") {
        const gchar* pin = nullptr;
        g_variant_get(params, "(&o&s)", nullptr, &pin);
        BLE_LOGI("agent: PIN for %s is %s", dev.c_str(), pin);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (m == "DisplayPasskey") {
        guint32 pk = 0;
        guint16 entered = 0;
        g_variant_get(params, "(&ouq)", nullptr, &pk, &entered);
        BLE_LOGI("agent: passkey for %s is %06u (%u typed)", dev.c_str(), pk, entered);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (m == "RequestConfirmation") {
        guint32 pk = 0;
        g_variant_get(params, "(&ou)", nullptr, &pk);
        if (!d->opts.agentAutoAccept) return reject("auto-accept disabled");
        BLE_LOGI("agent: confirming passkey %06u for %s", pk, dev.c_str());
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (m == "RequestAuthorization" || m == "AuthorizeService") {
        if (!d->opts.agentAutoAccept) return reject("auto-accept disabled");
        BLE_LOGI("agent: authorized %s (%s)", dev.c_str(), method);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotSupported", method);
    }
}

}  // namespace blemgr
