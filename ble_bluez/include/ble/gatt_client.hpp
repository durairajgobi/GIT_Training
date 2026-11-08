// SPDX-License-Identifier: MIT
// Client-side GATT operations on a remote characteristic (org.bluez.GattCharacteristic1).
// Notifications arrive through BleManager::subscribeValues().
#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ble {

class GattClient {
public:
    using ReadCallback = std::function<void(bool ok, const std::vector<uint8_t>& value,
                                            const std::string& error)>;
    using DoneCallback = std::function<void(bool ok, const std::string& error)>;

    GattClient(GDBusConnection* conn, std::string charPath) : conn_(conn), path_(std::move(charPath)) {}

    void read(ReadCallback cb, uint16_t offset = 0);
    // withResponse=false uses "write-without-response" (Write Command).
    void write(const std::vector<uint8_t>& value, DoneCallback cb, bool withResponse = true);
    void startNotify(DoneCallback cb);
    void stopNotify(DoneCallback cb);

private:
    GDBusConnection* conn_;
    std::string path_;
};

}  // namespace ble
