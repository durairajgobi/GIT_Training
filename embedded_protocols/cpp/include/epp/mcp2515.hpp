// mcp2515.hpp - Microchip MCP2515 stand-alone CAN controller over SPI.
//
// Typical use on an MCU or a Raspberry Pi without kernel driver:
//   epp::Mcp2515<epp::SpiDevice> can(spi);
//   can.reset(); can.setBitrate(8'000'000, 500'000); can.setMode(Mode::Normal);
//   can.send(frame); if (can.receive(frame) == Status::Ok) ...
//
// Also exposes an ep_can_ops_t table so the chip plugs into the generic C
// driver core (ep_can_t: software filters, RX queue, bus-off tracking).
#pragma once

#include <cstring>

#include "epp/core.hpp"
#include "ep/ep_can.h"

namespace epp {

template <class Spi>
class Mcp2515 {
public:
    enum class Mode : std::uint8_t { Normal = 0, Sleep = 1, Loopback = 2, ListenOnly = 3, Config = 4 };

    // Instructions
    static constexpr std::uint8_t kReset = 0xC0, kRead = 0x03, kWrite = 0x02, kBitMod = 0x05;
    static constexpr std::uint8_t kReadStatus = 0xA0, kLoadTx = 0x40, kRts = 0x80, kReadRx = 0x90;
    // Registers
    static constexpr std::uint8_t CANSTAT = 0x0E, CANCTRL = 0x0F, TEC = 0x1C, REC = 0x1D;
    static constexpr std::uint8_t CNF3 = 0x28, CNF2 = 0x29, CNF1 = 0x2A, CANINTE = 0x2B, CANINTF = 0x2C, EFLG = 0x2D;
    static constexpr std::uint8_t TXB0CTRL = 0x30, RXB0CTRL = 0x60, RXB1CTRL = 0x70;

    explicit Mcp2515(Spi &spi) : spi_(spi) {}

    Status reset()
    {
        const std::uint8_t cmd = kReset;
        EPP_TRY(spi_.transfer(Bytes(&cmd, 1), MutBytes()));
        // after reset the chip is in configuration mode
        auto st = readReg(CANSTAT);
        if (!st)
            return st.status();
        return (st.value() >> 5) == static_cast<std::uint8_t>(Mode::Config) ? Status::Ok : Status::Io;
    }

    // osc_hz: crystal frequency. Must be in Config mode.
    Status setBitrate(std::uint32_t osc_hz, std::uint32_t bitrate, std::uint16_t sp_permille = 0)
    {
        ep_can_bittiming_t bt;
        EPP_TRY(toStatus(ep_can_calc_bittiming(osc_hz / 2, bitrate, sp_permille, &ep_can_btc_mcp2515, &bt)));
        // tseg1 = PRSEG + PHSEG1, each 1..8
        std::uint16_t prseg = static_cast<std::uint16_t>(bt.tseg1 / 2);
        if (prseg < 1)
            prseg = 1;
        std::uint16_t ps1 = static_cast<std::uint16_t>(bt.tseg1 - prseg);
        if (ps1 > 8) {
            prseg = static_cast<std::uint16_t>(prseg + ps1 - 8);
            ps1 = 8;
        }
        if (prseg > 8 || ps1 < 1 || bt.tseg2 < 2 || bt.tseg2 > 8)
            return Status::Unsupported;
        const std::uint8_t cnf[3] = {
            static_cast<std::uint8_t>(bt.tseg2 - 1),                                       // CNF3
            static_cast<std::uint8_t>(0x80 | ((ps1 - 1) << 3) | (prseg - 1)),              // CNF2 (BTLMODE)
            static_cast<std::uint8_t>(((bt.sjw - 1) << 6) | (bt.brp - 1)),                 // CNF1
        };
        bittiming_ = bt;
        return writeRegs(CNF3, Bytes(cnf, 3));
    }

    Status setMode(Mode m)
    {
        EPP_TRY(bitModify(CANCTRL, 0xE0, static_cast<std::uint8_t>(static_cast<std::uint8_t>(m) << 5)));
        for (int i = 0; i < 10; i++) {
            auto st = readReg(CANSTAT);
            if (!st)
                return st.status();
            if ((st.value() >> 5) == static_cast<std::uint8_t>(m))
                return Status::Ok;
        }
        return Status::Timeout;
    }

    // Accept everything into both RX buffers, enable RX interrupts (INT pin).
    Status configureReceive()
    {
        EPP_TRY(writeReg(RXB0CTRL, 0x64)); // RXM=11 (any), BUKT rollover
        EPP_TRY(writeReg(RXB1CTRL, 0x60));
        return writeReg(CANINTE, 0x03);
    }

    Status send(const ep_can_frame_t &f)
    {
        if (!ep_can_frame_valid(&f) || (f.flags & EP_CAN_FLAG_FD))
            return Status::Param;
        auto status = readStatus();
        if (!status)
            return status.status();
        // READ STATUS bits 2/4/6 = TXB0/1/2 TXREQ
        int buf = -1;
        for (int i = 0; i < 3; i++)
            if (!(status.value() & (0x04 << (2 * i)))) {
                buf = i;
                break;
            }
        if (buf < 0)
            return Status::Busy;
        std::uint8_t tx[1 + 13];
        tx[0] = static_cast<std::uint8_t>(kLoadTx | (buf << 1)); // 0x40 / 0x42 / 0x44
        encodeId(f, tx + 1);
        tx[5] = static_cast<std::uint8_t>(f.len | ((f.flags & EP_CAN_FLAG_RTR) ? 0x40 : 0));
        std::memcpy(tx + 6, f.data, f.len);
        EPP_TRY(spi_.transfer(Bytes(tx, 6u + f.len), MutBytes()));
        const std::uint8_t rts = static_cast<std::uint8_t>(kRts | (1 << buf));
        return spi_.transfer(Bytes(&rts, 1), MutBytes());
    }

    Status receive(ep_can_frame_t &f)
    {
        auto status = readStatus();
        if (!status)
            return status.status();
        int buf;
        if (status.value() & 0x01)
            buf = 0;
        else if (status.value() & 0x02)
            buf = 1;
        else
            return Status::Again;
        const std::uint8_t cmd = static_cast<std::uint8_t>(kReadRx | (buf << 2)); // auto-clears RXnIF
        std::uint8_t r[13];
        EPP_TRY(spi_.writeThenRead(Bytes(&cmd, 1), MutBytes(r, 13)));
        std::memset(&f, 0, sizeof(f));
        decodeId(r, f);
        f.len = static_cast<std::uint8_t>(r[4] & 0x0F);
        if (f.len > 8)
            f.len = 8;
        if (r[4] & 0x40)
            f.flags |= EP_CAN_FLAG_RTR;
        std::memcpy(f.data, r + 5, f.len);
        return Status::Ok;
    }

    Status errorCounters(std::uint16_t &tec, std::uint16_t &rec)
    {
        std::uint8_t b[2];
        EPP_TRY(readRegs(TEC, MutBytes(b, 2)));
        auto eflg = readReg(EFLG);
        if (!eflg)
            return eflg.status();
        tec = (eflg.value() & 0x20) ? 256 : b[0]; // TXBO: bus-off
        rec = b[1];
        return Status::Ok;
    }

    const ep_can_bittiming_t &bittiming() const { return bittiming_; }

    // ---- low-level register access ----
    Result<std::uint8_t> readReg(std::uint8_t addr)
    {
        std::uint8_t v;
        Status st = readRegs(addr, MutBytes(&v, 1));
        if (!ok(st))
            return st;
        return v;
    }
    Status readRegs(std::uint8_t addr, MutBytes out)
    {
        const std::uint8_t cmd[2] = {kRead, addr};
        return spi_.writeThenRead(Bytes(cmd, 2), out);
    }
    Status writeReg(std::uint8_t addr, std::uint8_t v)
    {
        return writeRegs(addr, Bytes(&v, 1));
    }
    Status writeRegs(std::uint8_t addr, Bytes data)
    {
        std::uint8_t tx[2 + 16];
        if (data.size() > 16)
            return Status::Param;
        tx[0] = kWrite;
        tx[1] = addr;
        std::memcpy(tx + 2, data.data(), data.size());
        return spi_.transfer(Bytes(tx, 2 + data.size()), MutBytes());
    }
    Status bitModify(std::uint8_t addr, std::uint8_t mask, std::uint8_t v)
    {
        const std::uint8_t tx[4] = {kBitMod, addr, mask, v};
        return spi_.transfer(Bytes(tx, 4), MutBytes());
    }
    Result<std::uint8_t> readStatus()
    {
        const std::uint8_t cmd = kReadStatus;
        std::uint8_t v;
        Status st = spi_.writeThenRead(Bytes(&cmd, 1), MutBytes(&v, 1));
        if (!ok(st))
            return st;
        return v;
    }

    // ---- plug into the C driver core ----
    static const ep_can_ops_t &canOps()
    {
        static const ep_can_ops_t ops = {
            [](void *ctx, const ep_can_bittiming_t *, const ep_can_bittiming_t *) -> ep_status_t {
                (void)ctx;
                return EP_ERR_UNSUPPORTED; // use setBitrate(): needs the crystal frequency
            },
            [](void *ctx, ep_can_mode_t m) -> ep_status_t {
                static const Mode map[] = {Mode::Normal, Mode::Loopback, Mode::ListenOnly, Mode::Sleep};
                return static_cast<ep_status_t>(static_cast<Mcp2515 *>(ctx)->setMode(map[m]));
            },
            [](void *ctx, const ep_can_frame_t *f) -> ep_status_t {
                return static_cast<ep_status_t>(static_cast<Mcp2515 *>(ctx)->send(*f));
            },
            [](void *ctx, std::uint16_t *tec, std::uint16_t *rec) -> ep_status_t {
                return static_cast<ep_status_t>(static_cast<Mcp2515 *>(ctx)->errorCounters(*tec, *rec));
            },
            nullptr,
        };
        return ops;
    }

    // Drain the chip into the C core (call from the INT pin handler/task).
    void pollInto(ep_can_t &core)
    {
        ep_can_frame_t f;
        while (receive(f) == Status::Ok)
            ep_can_isr_rx(&core, &f);
    }

    static void encodeId(const ep_can_frame_t &f, std::uint8_t *o)
    {
        if (f.flags & EP_CAN_FLAG_EXT) {
            std::uint32_t sid = f.id >> 18, eid = f.id & 0x3FFFF;
            o[0] = static_cast<std::uint8_t>(sid >> 3);
            o[1] = static_cast<std::uint8_t>(((sid & 7) << 5) | 0x08 | (eid >> 16));
            o[2] = static_cast<std::uint8_t>(eid >> 8);
            o[3] = static_cast<std::uint8_t>(eid);
        } else {
            o[0] = static_cast<std::uint8_t>(f.id >> 3);
            o[1] = static_cast<std::uint8_t>((f.id & 7) << 5);
            o[2] = o[3] = 0;
        }
    }
    static void decodeId(const std::uint8_t *r, ep_can_frame_t &f)
    {
        std::uint32_t sid = (static_cast<std::uint32_t>(r[0]) << 3) | (r[1] >> 5);
        if (r[1] & 0x08) {
            f.flags |= EP_CAN_FLAG_EXT;
            f.id = (sid << 18) | (static_cast<std::uint32_t>(r[1] & 3) << 16) |
                   (static_cast<std::uint32_t>(r[2]) << 8) | r[3];
        } else {
            f.id = sid;
        }
    }

private:
    Spi &spi_;
    ep_can_bittiming_t bittiming_{};
};

}  // namespace epp
