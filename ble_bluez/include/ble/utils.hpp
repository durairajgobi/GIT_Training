// SPDX-License-Identifier: MIT
// Pure helpers (no GLib / D-Bus) so they can be unit-tested on any host.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ble {

// "AA:BB:CC:DD:EE:FF" (case-insensitive).
bool isValidAddress(const std::string& address);

// Upper-cases an address; throws std::invalid_argument if invalid.
std::string normalizeAddress(const std::string& address);

// "AA:BB:CC:DD:EE:FF" -> "dev_AA_BB_CC_DD_EE_FF" (BlueZ object-path leaf).
std::string addressToPathLeaf(const std::string& address);

// Builds "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF".
std::string devicePath(const std::string& adapterPath, const std::string& address);

// Expands 16/32-bit UUIDs onto the Bluetooth base UUID, lower-cases 128-bit
// ones. "180F" -> "0000180f-0000-1000-8000-00805f9b34fb".
// Throws std::invalid_argument on malformed input.
std::string normalizeUuid(const std::string& uuid);

// Accepts "0102ff", "01 02 ff", "01:02:ff", "0x0102ff". Throws on bad input.
std::vector<uint8_t> parseHex(const std::string& text);

// {0x01,0xab} -> "01 ab"
std::string toHex(const std::vector<uint8_t>& data, char sep = ' ');

// Printable ASCII rendering ('.' for non-printables).
std::string toPrintable(const std::vector<uint8_t>& data);

// Friendly name for well-known 16-bit service/characteristic UUIDs, or "".
std::string uuidName(const std::string& uuid);

}  // namespace ble
