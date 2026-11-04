// SPDX-License-Identifier: MIT
// Core value types shared by the BLE manager, its backends and applications.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blemgr {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------- status --

enum class Error {
    None,
    InvalidArgument,
    NotFound,       // object unknown to the stack (e.g. device never seen)
    NotReady,       // adapter off / manager not started
    NotConnected,
    Busy,           // operation already in progress
    Timeout,
    AuthFailed,
    NotSupported,
    NotPermitted,
    Cancelled,
    Failed,
};

const char* toString(Error e);

class Status {
public:
    Status() = default;
    Status(Error code, std::string message = {}) : code_(code), message_(std::move(message)) {}

    bool ok() const { return code_ == Error::None; }
    Error code() const { return code_; }
    const std::string& message() const { return message_; }
    std::string toString() const;

private:
    Error code_ = Error::None;
    std::string message_;
};

using StatusCallback = std::function<void(const Status&)>;
using ReadCallback = std::function<void(const Status&, const Bytes&)>;

// ------------------------------------------------------------- identity --

/// Normalises a Bluetooth UUID to lowercase 128-bit form.
/// Accepts 16-bit ("180d", "0x180D"), 32-bit and 128-bit (with or without dashes).
std::optional<std::string> normalizeUuid(std::string_view in);

/// Normalises a BD_ADDR to "AA:BB:CC:DD:EE:FF". Accepts ':', '-' or '_' separators.
std::optional<std::string> normalizeAddress(std::string_view in);

/// Identifies a remote characteristic by (service UUID, characteristic UUID).
struct CharRef {
    std::string service;
    std::string characteristic;

    bool operator==(const CharRef& o) const {
        return service == o.service && characteristic == o.characteristic;
    }
    bool operator<(const CharRef& o) const {
        return service != o.service ? service < o.service : characteristic < o.characteristic;
    }
    /// Returns a copy with both UUIDs normalised, or nullopt if either is invalid.
    std::optional<CharRef> normalized() const;
    std::string toString() const;
};

// --------------------------------------------------------------- remote --

enum class AddressType { Public, Random };

struct DeviceInfo {
    std::string address;
    AddressType addressType = AddressType::Public;
    std::string name;
    std::optional<int16_t> rssi;      // present only while the device is advertising in range
    std::optional<int16_t> txPower;
    std::vector<std::string> serviceUuids;
    std::map<uint16_t, Bytes> manufacturerData;
    std::map<std::string, Bytes> serviceData;
    bool paired = false;
    bool trusted = false;
    bool connected = false;
    bool servicesResolved = false;
};

struct GattCharacteristicInfo {
    std::string uuid;
    std::vector<std::string> flags;  // BlueZ flag names: "read", "notify", ...
    bool hasFlag(std::string_view f) const;
};

struct GattServiceInfo {
    std::string uuid;
    bool primary = true;
    std::vector<GattCharacteristicInfo> characteristics;
};

enum class WriteType { WithResponse, WithoutResponse };

enum class AdapterState { Unavailable, Off, On };
const char* toString(AdapterState s);

struct ScanFilter {
    std::vector<std::string> serviceUuids;  // match any; also pushed down to the controller
    std::optional<int16_t> minRssi;         // also pushed down to the controller
    std::string namePrefix;                 // host-side only
    std::vector<std::string> addresses;     // host-side allow-list
    bool duplicateData = true;              // report every advertisement (RSSI tracking)

    bool matches(const DeviceInfo& d) const;
    /// Stable textual key, used to detect filter changes.
    std::string key() const;
};

/// Per-device connection life cycle as seen by the manager.
enum class ConnState {
    Disconnected,
    WaitingAdvert,  // managed device unknown to the stack; background scan until it shows up
    WaitingRetry,   // managed device, back-off timer running
    Queued,         // waiting for the single connect slot / a free connection
    Connecting,
    Discovering,    // link up, GATT discovery running
    Ready,          // services resolved, subscriptions active
    Disconnecting,
};
const char* toString(ConnState s);

struct ConnectOptions {
    bool autoReconnect = true;           // keep the link up until disconnect() is called
    bool pair = false;                   // bond after the first successful connection
    std::vector<CharRef> subscriptions;  // notifications (re)enabled on every Ready
};

// ----------------------------------------------------- local peripheral --

enum CharProperty : uint32_t {
    kPropRead = 1u << 0,
    kPropWrite = 1u << 1,
    kPropWriteNoResponse = 1u << 2,
    kPropNotify = 1u << 3,
    kPropIndicate = 1u << 4,
    kPropEncryptRead = 1u << 5,
    kPropEncryptWrite = 1u << 6,
};

std::vector<std::string> charPropertyFlags(uint32_t props);

/// A characteristic hosted by this device (GATT server role).
/// Callbacks run on the manager's event loop thread and must not block.
struct LocalCharacteristic {
    std::string uuid;
    uint32_t properties = kPropRead;
    Bytes value;                                   // initial / cached value
    std::function<Bytes()> onRead;                 // optional dynamic value
    std::function<Status(const Bytes&)> onWrite;   // optional; reject by returning an error
};

struct LocalService {
    std::string uuid;
    bool primary = true;
    std::vector<LocalCharacteristic> characteristics;
};

struct AdvertisementData {
    std::string localName;
    std::vector<std::string> serviceUuids;
    std::map<uint16_t, Bytes> manufacturerData;
    std::map<std::string, Bytes> serviceData;
    std::optional<uint16_t> appearance;
    bool connectable = true;
    bool includeTxPower = false;
};

// ---------------------------------------------------------------- utils --

std::string toHex(const Bytes& b);
std::optional<Bytes> parseHex(std::string_view s);  // accepts "0a0B", "0a:0b", "0a 0b"

}  // namespace blemgr
