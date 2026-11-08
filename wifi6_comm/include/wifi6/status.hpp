// SPDX-License-Identifier: MIT
// Result codes shared by every layer. The library is built without
// exceptions, so all fallible operations return a Status.
#pragma once

#include <cstdint>

namespace wifi6 {

enum class Status : uint8_t {
    Ok = 0,
    Timeout,
    WouldBlock,     // non-blocking operation has nothing to do right now
    NotConnected,
    NotSupported,
    InvalidArg,
    InvalidState,
    IoError,
    BufferFull,
    ParseError,
    CrcError,
    AuthFailed,
    NoNetwork,
    Busy,
};

const char* toString(Status s) noexcept;

constexpr bool ok(Status s) noexcept { return s == Status::Ok; }

}  // namespace wifi6
