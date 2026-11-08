// SPDX-License-Identifier: MIT
// org.bluez.Adapter1 wrapper.
#pragma once

#include <gio/gio.h>

#include <optional>
#include <string>
#include <vector>

namespace ble {

struct DiscoveryFilter {
    enum class Transport { Auto, LE, BREDR };
    Transport transport = Transport::LE;
    std::optional<int16_t> rssi;             // only report devices stronger than this
    std::vector<std::string> uuids;          // only devices advertising these services
    std::string pattern;                     // address/name prefix (BlueZ >= 5.54)
    bool duplicateData = true;               // report every advertisement (RSSI updates)
    bool discoverable = false;               // only devices in discoverable mode
};

class Adapter {
public:
    Adapter(GDBusConnection* conn, std::string path);

    const std::string& path() const { return path_; }
    std::string address() const;
    std::string name() const;

    bool powered() const;
    void setPowered(bool on);
    void setDiscoverable(bool on, unsigned timeoutSec = 0);
    void setPairable(bool on, unsigned timeoutSec = 0);
    void setAlias(const std::string& alias);
    bool discovering() const;

    void setDiscoveryFilter(const DiscoveryFilter& filter);
    void startDiscovery();
    void stopDiscovery();       // ignores "not discovering" errors
    void removeDevice(const std::string& devicePath);

private:
    GDBusConnection* conn_;
    std::string path_;
};

}  // namespace ble
