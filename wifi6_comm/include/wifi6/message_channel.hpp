// SPDX-License-Identifier: MIT
// Framed messaging over a datagram transport with optional reliability:
// reliable frames are ACKed, retransmitted with exponential timeout and
// de-duplicated on the receiver. All buffers are statically sized.
#pragma once

#include <array>
#include <cstdint>

#include "wifi6/frame.hpp"
#include "wifi6/transport.hpp"

namespace wifi6 {

class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual void onMessage(MsgType type, uint16_t seq, const uint8_t* payload, size_t len,
                           const Endpoint& from) noexcept = 0;
    virtual void onDelivered(uint16_t /*seq*/) noexcept {}
    virtual void onDeliveryFailed(uint16_t /*seq*/) noexcept {}
};

struct ChannelStats {
    uint32_t txFrames = 0;
    uint32_t rxFrames = 0;
    uint32_t retransmits = 0;
    uint32_t crcErrors = 0;
    uint32_t parseErrors = 0;
    uint32_t duplicates = 0;
    uint32_t deliveryFailures = 0;
};

class MessageChannel {
public:
    static constexpr size_t kMaxPending = 8;
    static constexpr size_t kDedupWindow = 16;
    static constexpr size_t kMaxRxPerPoll = 32;

    MessageChannel(IDatagramTransport& transport, IMessageHandler& handler, uint16_t initialSeq = 1,
                   uint32_t ackTimeoutMs = 250, uint8_t maxRetries = 3) noexcept;

    void setPeer(const Endpoint& peer) noexcept {
        peer_ = peer;
        peerSet_ = true;
    }

    Status send(MsgType type, const uint8_t* payload, size_t len, bool reliable, uint64_t nowMs,
                uint16_t* seqOut = nullptr) noexcept;
    Status sendTo(const Endpoint& to, MsgType type, const uint8_t* payload, size_t len) noexcept;

    // Drain received datagrams and service retransmission timers.
    void poll(uint64_t nowMs) noexcept;

    const ChannelStats& stats() const noexcept { return stats_; }
    size_t pendingCount() const noexcept;

private:
    struct Pending {
        bool used = false;
        uint16_t seq = 0;
        uint8_t retries = 0;
        uint64_t deadlineMs = 0;
        Endpoint dest{};
        size_t len = 0;
        uint8_t data[kMaxFrameSize];
    };

    void handleDatagram(const uint8_t* data, size_t len, const Endpoint& from) noexcept;
    bool isDuplicate(uint16_t seq) const noexcept;
    void rememberSeq(uint16_t seq) noexcept;

    IDatagramTransport& transport_;
    IMessageHandler& handler_;
    Endpoint peer_{};
    bool peerSet_ = false;
    uint16_t nextSeq_;
    uint32_t ackTimeoutMs_;
    uint8_t maxRetries_;
    ChannelStats stats_{};
    std::array<Pending, kMaxPending> pending_{};
    std::array<uint32_t, kDedupWindow> recent_{};  // bit16 = valid, low 16 = seq
    size_t recentIdx_ = 0;
    uint8_t txBuf_[kMaxFrameSize]{};
    uint8_t rxBuf_[kMaxFrameSize + 1]{};
};

}  // namespace wifi6
