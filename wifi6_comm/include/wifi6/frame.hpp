// SPDX-License-Identifier: MIT
// Application framing carried in UDP datagrams over the Wi-Fi link.
//
//   0      2    3     4      5     6     8     10          10+N    14+N
//   +------+----+-----+------+-----+-----+-----+-- ... --+-------+
//   |magic |ver |type |flags | rsv | seq | len | payload | CRC32 |
//   +------+----+-----+------+-----+-----+-----+-- ... --+-------+
//
// All multi-byte fields are big-endian. CRC32 covers header + payload.
// The maximum frame fits a 1500-byte Wi-Fi MTU after IPv4/UDP headers.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wifi6/status.hpp"

namespace wifi6 {

enum class MsgType : uint8_t {
    Heartbeat = 0x01,
    Telemetry = 0x02,
    Command = 0x03,
    Ack = 0x04,
    Response = 0x05,
};

constexpr uint16_t kFrameMagic = 0x5736;  // "W6"
constexpr uint8_t kFrameVersion = 1;
constexpr size_t kFrameHeaderSize = 10;
constexpr size_t kFrameCrcSize = 4;
constexpr size_t kMaxPayload = 1200;
constexpr size_t kMaxFrameSize = kFrameHeaderSize + kMaxPayload + kFrameCrcSize;

constexpr uint8_t kFlagAckRequired = 0x01;

// Non-owning view into a decoded datagram.
struct FrameView {
    MsgType type = MsgType::Heartbeat;
    uint8_t flags = 0;
    uint16_t seq = 0;
    const uint8_t* payload = nullptr;
    uint16_t payloadLen = 0;

    bool ackRequired() const noexcept { return (flags & kFlagAckRequired) != 0; }
};

Status encodeFrame(MsgType type, uint8_t flags, uint16_t seq, const uint8_t* payload, size_t payloadLen,
                   uint8_t* out, size_t cap, size_t& written) noexcept;

Status decodeFrame(const uint8_t* in, size_t len, FrameView& out) noexcept;

}  // namespace wifi6
