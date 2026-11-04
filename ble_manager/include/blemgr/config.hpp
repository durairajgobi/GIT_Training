// SPDX-License-Identifier: MIT
// INI-style daemon configuration, see config/blemgrd.conf for the reference.
#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "blemgr/ble_manager.hpp"
#include "blemgr/bluez_backend.hpp"
#include "blemgr/log.hpp"

namespace blemgr {

struct ManagedDevice {
    std::string address;
    ConnectOptions options;
};

struct PeripheralConfig {
    bool enabled = false;
    std::string name = "TCU-BLE";
    unsigned notifyIntervalMs = 5000;
};

struct DaemonConfig {
    LogLevel logLevel = LogLevel::Info;
    ManagerConfig manager;
    BluezOptions bluez;
    std::vector<ManagedDevice> devices;
    PeripheralConfig peripheral;
};

/// Parses the configuration; on failure returns nullopt and sets *error to
/// "line N: reason".
std::optional<DaemonConfig> parseConfig(std::istream& in, std::string* error);
std::optional<DaemonConfig> loadConfigFile(const std::string& path, std::string* error);

}  // namespace blemgr
