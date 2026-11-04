// SPDX-License-Identifier: MIT
#include "gdbus_util.hpp"

#include <cstring>

namespace blemgr::gdbus {

Status toStatus(const GError* err) {
    if (!err) return {};
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) return {Error::Cancelled, err->message};
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) return {Error::Timeout, err->message};
    if (!g_dbus_error_is_remote_error(err)) return {Error::Failed, err->message};

    gchar* raw = g_dbus_error_get_remote_error(err);
    std::string name = raw ? raw : "";
    g_free(raw);
    GError* copy = g_error_copy(err);
    g_dbus_error_strip_remote_error(copy);
    std::string msg = name + ": " + (copy->message ? copy->message : "");
    g_error_free(copy);

    struct Map {
        const char* suffix;
        Error code;
    };
    static const Map table[] = {
        {"org.bluez.Error.AlreadyConnected", Error::None},
        {"org.bluez.Error.AlreadyExists", Error::None},
        {"org.bluez.Error.InProgress", Error::Busy},
        {"org.bluez.Error.Busy", Error::Busy},
        {"org.bluez.Error.NotReady", Error::NotReady},
        {"org.bluez.Error.NotConnected", Error::NotConnected},
        {"org.bluez.Error.DoesNotExist", Error::NotFound},
        {"org.bluez.Error.NotAvailable", Error::NotFound},
        {"org.freedesktop.DBus.Error.UnknownObject", Error::NotFound},
        {"org.freedesktop.DBus.Error.UnknownMethod", Error::NotFound},
        {"org.freedesktop.DBus.Error.ServiceUnknown", Error::NotReady},
        {"org.freedesktop.DBus.Error.NoReply", Error::Timeout},
        {"org.bluez.Error.AuthenticationFailed", Error::AuthFailed},
        {"org.bluez.Error.AuthenticationCanceled", Error::AuthFailed},
        {"org.bluez.Error.AuthenticationRejected", Error::AuthFailed},
        {"org.bluez.Error.AuthenticationTimeout", Error::AuthFailed},
        {"org.bluez.Error.NotPermitted", Error::NotPermitted},
        {"org.bluez.Error.NotAuthorized", Error::NotPermitted},
        {"org.bluez.Error.NotSupported", Error::NotSupported},
        {"org.bluez.Error.InvalidArguments", Error::InvalidArgument},
        {"org.bluez.Error.InvalidLength", Error::InvalidArgument},
        {"org.bluez.Error.InvalidOffset", Error::InvalidArgument},
    };
    for (const auto& m : table)
        if (name == m.suffix) return {m.code, msg};
    return {Error::Failed, msg};
}

Bytes toBytes(GVariant* ay) {
    if (!ay || !g_variant_is_of_type(ay, G_VARIANT_TYPE_BYTESTRING)) return {};
    gsize n = 0;
    auto* p = static_cast<const uint8_t*>(g_variant_get_fixed_array(ay, &n, 1));
    return Bytes(p, p + n);
}

GVariant* fromBytes(const Bytes& b) {
    static const uint8_t empty = 0;
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, b.empty() ? &empty : b.data(),
                                     b.size(), 1);
}

GVariant* fromStrings(const std::vector<std::string>& v) {
    std::vector<const gchar*> p;
    p.reserve(v.size());
    for (const auto& s : v) p.push_back(s.c_str());
    return g_variant_new_strv(p.data(), static_cast<gssize>(p.size()));
}

VariantPtr prop(GDBusProxy* p, const char* name) {
    return VariantPtr(p ? g_dbus_proxy_get_cached_property(p, name) : nullptr);
}

std::string propString(GDBusProxy* p, const char* name) {
    auto v = prop(p, name);
    if (!v) return {};
    if (g_variant_is_of_type(v.get(), G_VARIANT_TYPE_STRING) ||
        g_variant_is_of_type(v.get(), G_VARIANT_TYPE_OBJECT_PATH))
        return g_variant_get_string(v.get(), nullptr);
    return {};
}

bool propBool(GDBusProxy* p, const char* name) {
    auto v = prop(p, name);
    return v && g_variant_is_of_type(v.get(), G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(v.get());
}

std::optional<int16_t> propInt16(GDBusProxy* p, const char* name) {
    auto v = prop(p, name);
    if (v && g_variant_is_of_type(v.get(), G_VARIANT_TYPE_INT16)) return g_variant_get_int16(v.get());
    return std::nullopt;
}

std::vector<std::string> propStrings(GDBusProxy* p, const char* name) {
    std::vector<std::string> out;
    auto v = prop(p, name);
    if (!v || !g_variant_is_of_type(v.get(), G_VARIANT_TYPE_STRING_ARRAY)) return out;
    gsize n = 0;
    const gchar** arr = g_variant_get_strv(v.get(), &n);
    for (gsize i = 0; i < n; ++i) out.emplace_back(arr[i]);
    g_free(arr);
    return out;
}

std::optional<std::string> addressFromPath(const std::string& path) {
    auto pos = path.find("/dev_");
    if (pos == std::string::npos || path.size() < pos + 5 + 17) return std::nullopt;
    std::string a = path.substr(pos + 5, 17);
    if (path.size() > pos + 22 && path[pos + 22] != '/') return std::nullopt;
    return normalizeAddress(a);
}

std::string devicePath(const std::string& adapterPath, const std::string& address) {
    std::string a = address;
    for (char& c : a)
        if (c == ':') c = '_';
    return adapterPath + "/dev_" + a;
}

bool hasPrefix(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

}  // namespace blemgr::gdbus
