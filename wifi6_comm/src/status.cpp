// SPDX-License-Identifier: MIT
#include "wifi6/status.hpp"

namespace wifi6 {

const char* toString(Status s) noexcept {
    switch (s) {
        case Status::Ok: return "ok";
        case Status::Timeout: return "timeout";
        case Status::WouldBlock: return "would-block";
        case Status::NotConnected: return "not-connected";
        case Status::NotSupported: return "not-supported";
        case Status::InvalidArg: return "invalid-arg";
        case Status::InvalidState: return "invalid-state";
        case Status::IoError: return "io-error";
        case Status::BufferFull: return "buffer-full";
        case Status::ParseError: return "parse-error";
        case Status::CrcError: return "crc-error";
        case Status::AuthFailed: return "auth-failed";
        case Status::NoNetwork: return "no-network";
        case Status::Busy: return "busy";
    }
    return "unknown";
}

}  // namespace wifi6
