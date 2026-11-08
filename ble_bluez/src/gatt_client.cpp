// SPDX-License-Identifier: MIT
#include "ble/gatt_client.hpp"

#include "ble/dbus.hpp"

namespace ble {
namespace {
std::string describe(const CallResult& r) {
    return r.errorName.empty() ? r.errorMessage : r.errorName + ": " + r.errorMessage;
}
}  // namespace

void GattClient::read(ReadCallback cb, uint16_t offset) {
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    if (offset) g_variant_builder_add(&opts, "{sv}", "offset", g_variant_new_uint16(offset));
    callAsync(conn_, path_, iface::kGattChrc, "ReadValue", g_variant_new("(a{sv})", &opts), 10000,
              [cb = std::move(cb)](CallResult r) {
                  std::vector<uint8_t> value;
                  if (r.ok) {
                      GVariant* ay = g_variant_get_child_value(r.reply.get(), 0);
                      value = variantBytes(ay);
                      g_variant_unref(ay);
                  }
                  if (cb) cb(r.ok, value, r.ok ? "" : describe(r));
              });
}

void GattClient::write(const std::vector<uint8_t>& value, DoneCallback cb, bool withResponse) {
    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&opts, "{sv}", "type",
                          g_variant_new_string(withResponse ? "request" : "command"));
    callAsync(conn_, path_, iface::kGattChrc, "WriteValue",
              g_variant_new("(@aya{sv})", bytesVariant(value), &opts), 10000,
              [cb = std::move(cb)](CallResult r) { if (cb) cb(r.ok, r.ok ? "" : describe(r)); });
}

void GattClient::startNotify(DoneCallback cb) {
    callAsync(conn_, path_, iface::kGattChrc, "StartNotify", nullptr, 10000,
              [cb = std::move(cb)](CallResult r) { if (cb) cb(r.ok, r.ok ? "" : describe(r)); });
}

void GattClient::stopNotify(DoneCallback cb) {
    callAsync(conn_, path_, iface::kGattChrc, "StopNotify", nullptr, 10000,
              [cb = std::move(cb)](CallResult r) { if (cb) cb(r.ok, r.ok ? "" : describe(r)); });
}

}  // namespace ble
