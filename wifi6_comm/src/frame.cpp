// SPDX-License-Identifier: MIT
#include "wifi6/frame.hpp"

#include <cstring>

#include "wifi6/byte_order.hpp"
#include "wifi6/crc32.hpp"

namespace wifi6 {

namespace {
bool knownType(uint8_t t) noexcept { return t >= 0x01 && t <= 0x05; }
}  // namespace

Status encodeFrame(MsgType type, uint8_t flags, uint16_t seq, const uint8_t* payload, size_t payloadLen,
                   uint8_t* out, size_t cap, size_t& written) noexcept {
    written = 0;
    if (payloadLen > kMaxPayload || (payloadLen > 0 && payload == nullptr) || out == nullptr)
        return Status::InvalidArg;
    const size_t total = kFrameHeaderSize + payloadLen + kFrameCrcSize;
    if (cap < total) return Status::BufferFull;

    putBe16(out + 0, kFrameMagic);
    out[2] = kFrameVersion;
    out[3] = static_cast<uint8_t>(type);
    out[4] = flags;
    out[5] = 0;
    putBe16(out + 6, seq);
    putBe16(out + 8, static_cast<uint16_t>(payloadLen));
    if (payloadLen) std::memcpy(out + kFrameHeaderSize, payload, payloadLen);
    putBe32(out + kFrameHeaderSize + payloadLen, crc32(out, kFrameHeaderSize + payloadLen));
    written = total;
    return Status::Ok;
}

Status decodeFrame(const uint8_t* in, size_t len, FrameView& out) noexcept {
    if (in == nullptr || len < kFrameHeaderSize + kFrameCrcSize) return Status::ParseError;
    if (getBe16(in) != kFrameMagic || in[2] != kFrameVersion) return Status::ParseError;
    const uint16_t payloadLen = getBe16(in + 8);
    if (payloadLen > kMaxPayload || kFrameHeaderSize + payloadLen + kFrameCrcSize != len) return Status::ParseError;

    const uint32_t expect = getBe32(in + kFrameHeaderSize + payloadLen);
    if (crc32(in, kFrameHeaderSize + payloadLen) != expect) return Status::CrcError;
    if (!knownType(in[3])) return Status::ParseError;

    out.type = static_cast<MsgType>(in[3]);
    out.flags = in[4];
    out.seq = getBe16(in + 6);
    out.payload = in + kFrameHeaderSize;
    out.payloadLen = payloadLen;
    return Status::Ok;
}

}  // namespace wifi6
