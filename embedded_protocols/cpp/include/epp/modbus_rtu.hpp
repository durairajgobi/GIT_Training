// modbus_rtu.hpp - Modbus RTU master and slave (RS-485 / RS-232 / RS-422).
//
// Frame:  [addr][fc][data ...][crc lo][crc hi]
// Frames are delimited by >= 3.5 character times of silence (t3.5).
//
// Port concept (blocking, provided by the board / Linux serial wrapper):
//   void   flushInput();
//   size_t write(const uint8_t *d, size_t n);      // queue for transmission
//   void   drain();                                 // wait until last stop bit sent
//   int    readByte(uint32_t timeout_us);           // -1 on timeout
//
// The RS-485 driver-enable turnaround lives below the Port (ep_serial_line
// on an MCU, TIOCSRS485 on Linux), so this code is transceiver-agnostic.
#pragma once

#include <cstring>

#include "epp/core.hpp"
#include "ep/ep_crc.h"

namespace epp {
namespace modbus {

enum class Fc : std::uint8_t {
    ReadCoils = 0x01,
    ReadDiscreteInputs = 0x02,
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleCoil = 0x05,
    WriteSingleRegister = 0x06,
    WriteMultipleCoils = 0x0F,
    WriteMultipleRegisters = 0x10,
};

enum class Exception : std::uint8_t {
    None = 0,
    IllegalFunction = 1,
    IllegalDataAddress = 2,
    IllegalDataValue = 3,
    ServerDeviceFailure = 4,
};

constexpr std::size_t kMaxAdu = 256;
constexpr std::uint16_t kMaxReadRegs = 125;
constexpr std::uint16_t kMaxWriteRegs = 123;

inline std::size_t appendCrc(std::uint8_t *buf, std::size_t len)
{
    std::uint16_t c = ep_crc16_modbus(EP_CRC16_MODBUS_INIT, buf, len);
    buf[len] = static_cast<std::uint8_t>(c);        // CRC is sent low byte first
    buf[len + 1] = static_cast<std::uint8_t>(c >> 8);
    return len + 2;
}

inline bool crcOk(const std::uint8_t *buf, std::size_t len)
{
    return len >= 4 && ep_crc16_modbus(EP_CRC16_MODBUS_INIT, buf, len) == 0; // residue 0
}

// ---------------------------------------------------------------------------
template <class Port>
class RtuMaster {
public:
    RtuMaster(Port &port, std::uint32_t t35_us, std::uint32_t response_timeout_us = 100000)
        : port_(port), t35_us_(t35_us), timeout_us_(response_timeout_us)
    {
    }

    Exception lastException() const { return exc_; }

    Status readHoldingRegisters(std::uint8_t slave, std::uint16_t addr, Span<std::uint16_t> out)
    {
        return readRegs(Fc::ReadHoldingRegisters, slave, addr, out);
    }
    Status readInputRegisters(std::uint8_t slave, std::uint16_t addr, Span<std::uint16_t> out)
    {
        return readRegs(Fc::ReadInputRegisters, slave, addr, out);
    }

    Status writeSingleRegister(std::uint8_t slave, std::uint16_t addr, std::uint16_t value)
    {
        std::uint8_t req[8] = {slave, static_cast<std::uint8_t>(Fc::WriteSingleRegister)};
        ep_put_be16(req + 2, addr);
        ep_put_be16(req + 4, value);
        std::uint8_t resp[8];
        return transact(req, 6, resp, 8, true); // echo of the request
    }

    Status writeMultipleRegisters(std::uint8_t slave, std::uint16_t addr, Span<const std::uint16_t> v)
    {
        if (v.empty() || v.size() > kMaxWriteRegs)
            return Status::Param;
        std::uint8_t req[kMaxAdu] = {slave, static_cast<std::uint8_t>(Fc::WriteMultipleRegisters)};
        ep_put_be16(req + 2, addr);
        ep_put_be16(req + 4, static_cast<std::uint16_t>(v.size()));
        req[6] = static_cast<std::uint8_t>(v.size() * 2);
        for (std::size_t i = 0; i < v.size(); i++)
            ep_put_be16(req + 7 + 2 * i, v[i]);
        std::uint8_t resp[8];
        return transact(req, 7 + 2 * v.size(), resp, 8, false);
    }

    Status readCoils(std::uint8_t slave, std::uint16_t addr, std::uint16_t count, MutBytes bits)
    {
        if (count == 0 || count > 2000 || bits.size() < (count + 7u) / 8u)
            return Status::Param;
        std::uint8_t req[8] = {slave, static_cast<std::uint8_t>(Fc::ReadCoils)};
        ep_put_be16(req + 2, addr);
        ep_put_be16(req + 4, count);
        std::size_t nbytes = (count + 7u) / 8u;
        std::uint8_t resp[kMaxAdu];
        EPP_TRY(transact(req, 6, resp, 3 + nbytes + 2, false));
        if (resp[2] != nbytes)
            return Status::Frame;
        std::memcpy(bits.data(), resp + 3, nbytes);
        return Status::Ok;
    }

    Status writeSingleCoil(std::uint8_t slave, std::uint16_t addr, bool on)
    {
        std::uint8_t req[8] = {slave, static_cast<std::uint8_t>(Fc::WriteSingleCoil)};
        ep_put_be16(req + 2, addr);
        ep_put_be16(req + 4, on ? 0xFF00 : 0x0000);
        std::uint8_t resp[8];
        return transact(req, 6, resp, 8, true);
    }

private:
    Status readRegs(Fc fc, std::uint8_t slave, std::uint16_t addr, Span<std::uint16_t> out)
    {
        if (out.empty() || out.size() > kMaxReadRegs || slave == 0)
            return Status::Param;
        std::uint8_t req[8] = {slave, static_cast<std::uint8_t>(fc)};
        ep_put_be16(req + 2, addr);
        ep_put_be16(req + 4, static_cast<std::uint16_t>(out.size()));
        std::uint8_t resp[kMaxAdu];
        EPP_TRY(transact(req, 6, resp, 3 + 2 * out.size() + 2, false));
        if (resp[2] != 2 * out.size())
            return Status::Frame;
        for (std::size_t i = 0; i < out.size(); i++)
            out[i] = ep_get_be16(resp + 3 + 2 * i);
        return Status::Ok;
    }

    // Receive bytes until t3.5 of silence.
    std::size_t receiveFrame(std::uint8_t *buf, std::size_t cap)
    {
        int b = port_.readByte(timeout_us_);
        if (b < 0)
            return 0;
        std::size_t n = 0;
        while (b >= 0) {
            if (n < cap)
                buf[n++] = static_cast<std::uint8_t>(b);
            b = port_.readByte(t35_us_);
        }
        return n;
    }

    Status transact(std::uint8_t *req, std::size_t len, std::uint8_t *resp, std::size_t expect, bool echo)
    {
        exc_ = Exception::None;
        len = appendCrc(req, len);
        port_.flushInput();
        if (port_.write(req, len) != len)
            return Status::Io;
        port_.drain();
        if (req[0] == 0)
            return Status::Ok; // broadcast: no reply
        std::size_t n = receiveFrame(resp, kMaxAdu);
        if (n == 0)
            return Status::Timeout;
        if (!crcOk(resp, n))
            return Status::Crc;
        if (resp[0] != req[0])
            return Status::Frame;
        if (resp[1] == (req[1] | 0x80)) {
            exc_ = n >= 5 ? static_cast<Exception>(resp[2]) : Exception::ServerDeviceFailure;
            return Status::Nack;
        }
        if (resp[1] != req[1] || n != expect)
            return Status::Frame;
        if (echo && std::memcmp(resp, req, len) != 0)
            return Status::Frame;
        return Status::Ok;
    }

    Port &port_;
    std::uint32_t t35_us_, timeout_us_;
    Exception exc_ = Exception::None;
};

// ---------------------------------------------------------------------------
// Slave. Map concept:
//   Exception readHolding(uint16_t addr, Span<uint16_t> out);
//   Exception writeHolding(uint16_t addr, Span<const uint16_t> in);
//   Exception readInput(uint16_t addr, Span<uint16_t> out);
//   Exception readCoils(uint16_t addr, uint16_t count, uint8_t *bits);
//   Exception writeCoil(uint16_t addr, bool on);
template <class Map>
class RtuSlave {
public:
    RtuSlave(Map &map, std::uint8_t address) : map_(map), addr_(address) {}

    // Returns the response length (0 = stay silent).
    std::size_t handle(const std::uint8_t *req, std::size_t len, std::uint8_t *resp)
    {
        if (!crcOk(req, len)) {
            crc_errors_++;
            return 0; // never answer a corrupted frame
        }
        bool broadcast = req[0] == 0;
        if (!broadcast && req[0] != addr_)
            return 0;
        len -= 2;
        const std::uint8_t fc = req[1];
        resp[0] = addr_;
        resp[1] = fc;
        std::size_t n = 0;
        Exception e = Exception::None;

        auto u16 = [&](std::size_t off) { return ep_get_be16(req + off); };

        switch (static_cast<Fc>(fc)) {
        case Fc::ReadHoldingRegisters:
        case Fc::ReadInputRegisters: {
            if (len != 6) { e = Exception::IllegalDataValue; break; }
            std::uint16_t a = u16(2), cnt = u16(4);
            if (cnt == 0 || cnt > kMaxReadRegs) { e = Exception::IllegalDataValue; break; }
            std::uint16_t regs[kMaxReadRegs];
            Span<std::uint16_t> out(regs, cnt);
            e = static_cast<Fc>(fc) == Fc::ReadHoldingRegisters ? map_.readHolding(a, out) : map_.readInput(a, out);
            if (e != Exception::None) break;
            resp[2] = static_cast<std::uint8_t>(2 * cnt);
            for (std::size_t i = 0; i < cnt; i++)
                ep_put_be16(resp + 3 + 2 * i, regs[i]);
            n = 3 + 2u * cnt;
            break;
        }
        case Fc::WriteSingleRegister: {
            if (len != 6) { e = Exception::IllegalDataValue; break; }
            std::uint16_t v = u16(4);
            e = map_.writeHolding(u16(2), Span<const std::uint16_t>(&v, 1));
            if (e != Exception::None) break;
            std::memcpy(resp, req, 6); // echo
            n = 6;
            break;
        }
        case Fc::WriteMultipleRegisters: {
            if (len < 7) { e = Exception::IllegalDataValue; break; }
            std::uint16_t a = u16(2), cnt = u16(4);
            if (cnt == 0 || cnt > kMaxWriteRegs || req[6] != 2 * cnt || len != 7u + 2u * cnt) {
                e = Exception::IllegalDataValue;
                break;
            }
            std::uint16_t regs[kMaxWriteRegs];
            for (std::size_t i = 0; i < cnt; i++)
                regs[i] = u16(7 + 2 * i);
            e = map_.writeHolding(a, Span<const std::uint16_t>(regs, cnt));
            if (e != Exception::None) break;
            std::memcpy(resp + 2, req + 2, 4);
            n = 6;
            break;
        }
        case Fc::ReadCoils: {
            if (len != 6) { e = Exception::IllegalDataValue; break; }
            std::uint16_t a = u16(2), cnt = u16(4);
            if (cnt == 0 || cnt > 2000) { e = Exception::IllegalDataValue; break; }
            std::size_t nb = (cnt + 7u) / 8u;
            std::memset(resp + 3, 0, nb);
            e = map_.readCoils(a, cnt, resp + 3);
            if (e != Exception::None) break;
            resp[2] = static_cast<std::uint8_t>(nb);
            n = 3 + nb;
            break;
        }
        case Fc::WriteSingleCoil: {
            if (len != 6) { e = Exception::IllegalDataValue; break; }
            std::uint16_t v = u16(4);
            if (v != 0xFF00 && v != 0x0000) { e = Exception::IllegalDataValue; break; }
            e = map_.writeCoil(u16(2), v == 0xFF00);
            if (e != Exception::None) break;
            std::memcpy(resp, req, 6);
            n = 6;
            break;
        }
        default:
            e = Exception::IllegalFunction;
            break;
        }

        if (broadcast)
            return 0; // execute, never reply
        if (e != Exception::None) {
            resp[1] = static_cast<std::uint8_t>(fc | 0x80);
            resp[2] = static_cast<std::uint8_t>(e);
            n = 3;
        }
        return appendCrc(resp, n);
    }

    // Blocking service loop step for a Port (see RtuMaster).
    template <class Port>
    bool poll(Port &port, std::uint32_t t35_us, std::uint32_t wait_us)
    {
        std::uint8_t req[kMaxAdu], resp[kMaxAdu];
        int b = port.readByte(wait_us);
        if (b < 0)
            return false;
        std::size_t n = 0;
        while (b >= 0) {
            if (n < sizeof(req))
                req[n++] = static_cast<std::uint8_t>(b);
            b = port.readByte(t35_us);
        }
        std::size_t r = handle(req, n, resp);
        if (r) {
            port.write(resp, r);
            port.drain();
        }
        return r != 0;
    }

    std::uint32_t crcErrors() const { return crc_errors_; }

private:
    Map &map_;
    std::uint8_t addr_;
    std::uint32_t crc_errors_ = 0;
};

}  // namespace modbus
}  // namespace epp
