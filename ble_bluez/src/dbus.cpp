// SPDX-License-Identifier: MIT
#include "ble/dbus.hpp"

namespace ble {

std::pair<std::string, std::string> takeError(GError* err) {
    if (!err) return {};
    std::string name;
    if (gchar* remote = g_dbus_error_get_remote_error(err)) {
        name = remote;
        g_free(remote);
        g_dbus_error_strip_remote_error(err);
    }
    std::string message = err->message ? err->message : "unknown error";
    g_error_free(err);
    return {name, message};
}

void throwIfError(GError* err, const std::string& context) {
    if (!err) return;
    auto [name, message] = takeError(err);
    throw DBusError(name, context + ": " + message);
}

VariantPtr callSync(GDBusConnection* conn, const std::string& path, const char* interface,
                    const char* method, GVariant* params, const GVariantType* replyType,
                    int timeoutMs) {
    GError* err = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(conn, iface::kBluezService, path.c_str(),
                                                  interface, method, params, replyType,
                                                  G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr, &err);
    throwIfError(err, std::string(interface) + "." + method + " on " + path);
    return VariantPtr(reply);
}

namespace {
void onAsyncReply(GObject* source, GAsyncResult* res, gpointer data) {
    std::unique_ptr<CallCallback> cb(static_cast<CallCallback*>(data));
    GError* err = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    CallResult result;
    if (err) {
        auto [name, message] = takeError(err);
        result.errorName = name;
        result.errorMessage = message;
    } else {
        result.ok = true;
        result.reply.reset(reply);
    }
    if (*cb) (*cb)(std::move(result));
}
}  // namespace

void callAsync(GDBusConnection* conn, const std::string& path, const char* interface,
               const char* method, GVariant* params, int timeoutMs, CallCallback cb) {
    g_dbus_connection_call(conn, iface::kBluezService, path.c_str(), interface, method, params,
                           nullptr, G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr, onAsyncReply,
                           new CallCallback(std::move(cb)));
}

void setProperty(GDBusConnection* conn, const std::string& path, const char* interface,
                 const char* name, GVariant* value) {
    callSync(conn, path, iface::kProperties, "Set",
             g_variant_new("(ssv)", interface, name, value));
}

VariantPtr getProperty(GDBusConnection* conn, const std::string& path, const char* interface,
                       const char* name) {
    VariantPtr reply = callSync(conn, path, iface::kProperties, "Get",
                                g_variant_new("(ss)", interface, name), G_VARIANT_TYPE("(v)"));
    GVariant* inner = nullptr;
    g_variant_get(reply.get(), "(v)", &inner);
    return VariantPtr(inner);
}

PropertyMap toPropertyMap(GVariant* asv) {
    PropertyMap out;
    if (!asv) return out;
    GVariantIter it;
    const gchar* key;
    GVariant* value;
    g_variant_iter_init(&it, asv);
    while (g_variant_iter_next(&it, "{&sv}", &key, &value)) out.emplace(key, VariantPtr(value));
    return out;
}

std::string variantString(GVariant* v, const std::string& def) {
    if (!v) return def;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) ||
        g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH))
        return g_variant_get_string(v, nullptr);
    return def;
}

bool variantBool(GVariant* v, bool def) {
    return (v && g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)) ? g_variant_get_boolean(v) : def;
}

std::vector<std::string> variantStringList(GVariant* v) {
    std::vector<std::string> out;
    if (!v || !g_variant_is_of_type(v, G_VARIANT_TYPE_STRING_ARRAY)) return out;
    gsize n = 0;
    const gchar** arr = g_variant_get_strv(v, &n);
    for (gsize i = 0; i < n; ++i) out.emplace_back(arr[i]);
    g_free(arr);
    return out;
}

std::vector<uint8_t> variantBytes(GVariant* v) {
    if (!v || !g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) return {};
    gsize n = 0;
    auto* p = static_cast<const uint8_t*>(g_variant_get_fixed_array(v, &n, sizeof(uint8_t)));
    return std::vector<uint8_t>(p, p + n);
}

GVariant* bytesVariant(const std::vector<uint8_t>& data) {
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data.data(), data.size(), sizeof(uint8_t));
}

GVariant* stringListVariant(const std::vector<std::string>& list) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_STRING_ARRAY);
    for (const auto& s : list) g_variant_builder_add(&b, "s", s.c_str());
    return g_variant_builder_end(&b);
}

namespace {
gboolean timeoutTrampoline(gpointer data) {
    auto* fn = static_cast<std::function<bool()>*>(data);
    return (*fn)() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}
void deleteTimeoutFn(gpointer data) { delete static_cast<std::function<bool()>*>(data); }
gboolean idleTrampoline(gpointer data) {
    (*static_cast<std::function<void()>*>(data))();
    return G_SOURCE_REMOVE;
}
void deleteIdleFn(gpointer data) { delete static_cast<std::function<void()>*>(data); }
}  // namespace

guint addTimeout(unsigned ms, std::function<bool()> fn) {
    return g_timeout_add_full(G_PRIORITY_DEFAULT, ms, timeoutTrampoline,
                              new std::function<bool()>(std::move(fn)), deleteTimeoutFn);
}

guint addIdle(std::function<void()> fn) {
    return g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idleTrampoline,
                           new std::function<void()>(std::move(fn)), deleteIdleFn);
}

}  // namespace ble
