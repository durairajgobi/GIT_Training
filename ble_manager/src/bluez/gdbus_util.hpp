// SPDX-License-Identifier: MIT
// Small RAII / conversion helpers around GLib + GDBus. Internal header.
#pragma once

#include <gio/gio.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "blemgr/types.hpp"

namespace blemgr::gdbus {

struct VariantUnref {
    void operator()(GVariant* v) const {
        if (v) g_variant_unref(v);
    }
};
using VariantPtr = std::unique_ptr<GVariant, VariantUnref>;

struct ObjectUnref {
    void operator()(gpointer p) const {
        if (p) g_object_unref(p);
    }
};
template <class T>
using ObjPtr = std::unique_ptr<T, ObjectUnref>;

struct ErrorFree {
    void operator()(GError* e) const {
        if (e) g_error_free(e);
    }
};
using ErrorPtr = std::unique_ptr<GError, ErrorFree>;

/// Maps a GDBus / BlueZ error to a Status.
Status toStatus(const GError* err);

// GVariant <-> C++ conversions
Bytes toBytes(GVariant* ay);
GVariant* fromBytes(const Bytes& b);  // floating "ay"
GVariant* fromStrings(const std::vector<std::string>& v);  // floating "as"

// Cached proxy properties
VariantPtr prop(GDBusProxy* p, const char* name);
std::string propString(GDBusProxy* p, const char* name);
bool propBool(GDBusProxy* p, const char* name);
std::optional<int16_t> propInt16(GDBusProxy* p, const char* name);
std::vector<std::string> propStrings(GDBusProxy* p, const char* name);

/// "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF[/...]" -> "AA:BB:CC:DD:EE:FF"
std::optional<std::string> addressFromPath(const std::string& path);
std::string devicePath(const std::string& adapterPath, const std::string& address);

bool hasPrefix(const std::string& s, const std::string& prefix);

}  // namespace blemgr::gdbus
