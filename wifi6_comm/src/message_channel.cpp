// SPDX-License-Identifier: MIT
#include "wifi6/message_channel.hpp"

namespace wifi6 {

MessageChannel::MessageChannel(IDatagramTransport& transport, IMessageHandler& handler, uint16_t initialSeq,
                               uint32_t ackTimeoutMs, uint8_t maxRetries) noexcept
    : transport_(transport),
      handler_(handler),
      nextSeq_(initialSeq),
      ackTimeoutMs_(ackTimeoutMs ? ackTimeoutMs : 1),
      maxRetries_(maxRetries) {}

size_t MessageChannel::pendingCount() const noexcept {
    size_t n = 0;
    for (const Pending& p : pending_) n += p.used ? 1 : 0;
    return n;
}

Status MessageChannel::send(MsgType type, const uint8_t* payload, size_t len, bool reliable, uint64_t nowMs,
                            uint16_t* seqOut) noexcept {
    if (!peerSet_) return Status::NotConnected;
    if (type == MsgType::Ack) return Status::InvalidArg;

    if (!reliable) {
        size_t n = 0;
        Status s = encodeFrame(type, 0, nextSeq_, payload, len, txBuf_, sizeof txBuf_, n);
        if (!ok(s)) return s;
        s = transport_.send(peer_, txBuf_, n);
        if (!ok(s)) return s;
        if (seqOut) *seqOut = nextSeq_;
        ++nextSeq_;
        ++stats_.txFrames;
        return Status::Ok;
    }

    Pending* slot = nullptr;
    for (Pending& p : pending_) {
        if (!p.used) {
            slot = &p;
            break;
        }
    }
    if (!slot) return Status::BufferFull;

    size_t n = 0;
    Status s = encodeFrame(type, kFlagAckRequired, nextSeq_, payload, len, slot->data, sizeof slot->data, n);
    if (!ok(s)) return s;
    s = transport_.send(peer_, slot->data, n);
    // A transient WouldBlock is fine: the retransmit timer will resend it.
    if (!ok(s) && s != Status::WouldBlock) return s;

    slot->used = true;
    slot->seq = nextSeq_;
    slot->retries = 0;
    slot->len = n;
    slot->dest = peer_;
    slot->deadlineMs = nowMs + ackTimeoutMs_;
    if (seqOut) *seqOut = nextSeq_;
    ++nextSeq_;
    ++stats_.txFrames;
    return Status::Ok;
}

Status MessageChannel::sendTo(const Endpoint& to, MsgType type, const uint8_t* payload, size_t len) noexcept {
    size_t n = 0;
    Status s = encodeFrame(type, 0, nextSeq_, payload, len, txBuf_, sizeof txBuf_, n);
    if (!ok(s)) return s;
    s = transport_.send(to, txBuf_, n);
    if (ok(s)) {
        ++nextSeq_;
        ++stats_.txFrames;
    }
    return s;
}

void MessageChannel::poll(uint64_t nowMs) noexcept {
    for (size_t i = 0; i < kMaxRxPerPoll; ++i) {
        size_t len = 0;
        Endpoint from;
        const Status s = transport_.receive(rxBuf_, sizeof rxBuf_, len, from);
        if (s == Status::WouldBlock) break;
        if (s == Status::ParseError) {
            ++stats_.parseErrors;  // oversize datagram
            continue;
        }
        if (!ok(s)) break;
        handleDatagram(rxBuf_, len, from);
    }

    for (Pending& p : pending_) {
        if (!p.used || nowMs < p.deadlineMs) continue;
        if (p.retries >= maxRetries_) {
            p.used = false;
            ++stats_.deliveryFailures;
            handler_.onDeliveryFailed(p.seq);
            continue;
        }
        (void)transport_.send(p.dest, p.data, p.len);
        ++p.retries;
        ++stats_.retransmits;
        // Exponential: 2x, 4x, 8x the base timeout.
        p.deadlineMs = nowMs + (static_cast<uint64_t>(ackTimeoutMs_) << p.retries);
    }
}

void MessageChannel::handleDatagram(const uint8_t* data, size_t len, const Endpoint& from) noexcept {
    FrameView f;
    const Status s = decodeFrame(data, len, f);
    if (s == Status::CrcError) {
        ++stats_.crcErrors;
        return;
    }
    if (!ok(s)) {
        ++stats_.parseErrors;
        return;
    }
    ++stats_.rxFrames;

    if (f.type == MsgType::Ack) {
        for (Pending& p : pending_) {
            if (p.used && p.seq == f.seq) {
                p.used = false;
                handler_.onDelivered(p.seq);
                break;
            }
        }
        return;
    }

    if (f.ackRequired()) {
        size_t n = 0;
        uint8_t ack[kFrameHeaderSize + kFrameCrcSize];
        if (ok(encodeFrame(MsgType::Ack, 0, f.seq, nullptr, 0, ack, sizeof ack, n))) (void)transport_.send(from, ack, n);
        if (isDuplicate(f.seq)) {
            ++stats_.duplicates;  // our earlier ACK was lost; don't deliver twice
            return;
        }
        rememberSeq(f.seq);
    }
    handler_.onMessage(f.type, f.seq, f.payload, f.payloadLen, from);
}

bool MessageChannel::isDuplicate(uint16_t seq) const noexcept {
    const uint32_t key = 0x10000u | seq;
    for (uint32_t r : recent_)
        if (r == key) return true;
    return false;
}

void MessageChannel::rememberSeq(uint16_t seq) noexcept {
    recent_[recentIdx_] = 0x10000u | seq;
    recentIdx_ = (recentIdx_ + 1) % kDedupWindow;
}

}  // namespace wifi6
