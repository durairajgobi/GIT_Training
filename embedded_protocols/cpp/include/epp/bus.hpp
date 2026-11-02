// bus.hpp - C++ handles over the C SPI / I2C device layer.
//
// Device drivers in this library are templates over a "bus device" type so
// they work with these adapters, with a hand-written register-level class,
// or with a test fake - with zero virtual-call overhead.
//
//   SpiDevice concept:
//     Status transfer(Bytes tx, MutBytes rx);          // one CS window
//     Status writeThenRead(Bytes tx, MutBytes rx);     // one CS window
//   I2cDevice concept:
//     Status write(Bytes data);
//     Status writeRead(Bytes w, MutBytes r);           // repeated START
//     Status read(MutBytes r);
#pragma once

#include "epp/core.hpp"
#include "ep/ep_i2c.h"
#include "ep/ep_spi.h"

namespace epp {

class SpiDevice {
public:
    SpiDevice(ep_spi_bus_t &bus, std::uint8_t cs, const ep_spi_config_t &cfg)
    {
        init_ = toStatus(ep_spi_device_init(&dev_, &bus, cs, &cfg));
    }
    Status initStatus() const { return init_; }

    Status transfer(Bytes tx, MutBytes rx)
    {
        if (!tx.empty() && !rx.empty() && tx.size() != rx.size())
            return Status::Param;
        std::size_t n = tx.empty() ? rx.size() : tx.size();
        return toStatus(ep_spi_transfer(&dev_, tx.data(), rx.data(), n));
    }
    Status writeThenRead(Bytes tx, MutBytes rx)
    {
        return toStatus(ep_spi_write_then_read(&dev_, tx.data(), tx.size(), rx.data(), rx.size()));
    }
    ep_spi_device_t &raw() { return dev_; }

private:
    ep_spi_device_t dev_{};
    Status init_ = Status::State;
};

class I2cDevice {
public:
    I2cDevice(ep_i2c_bus_t &bus, std::uint16_t addr, std::uint8_t reg_addr_len = 1, bool pec = false)
    {
        dev_.bus = &bus;
        dev_.addr = addr;
        dev_.addr_flags = 0;
        dev_.reg_addr_len = reg_addr_len;
        dev_.pec = pec;
    }
    Status write(Bytes d) { return toStatus(ep_i2c_write(&dev_, d.data(), d.size())); }
    Status read(MutBytes r) { return toStatus(ep_i2c_read(&dev_, r.data(), r.size())); }
    Status writeRead(Bytes w, MutBytes r)
    {
        return toStatus(ep_i2c_write_read(&dev_, w.data(), w.size(), r.data(), r.size()));
    }

    // Typed big-endian register access: readReg<uint16_t>(0x05)
    template <class T>
    Result<T> readReg(std::uint8_t reg)
    {
        static_assert(std::is_unsigned<T>::value && sizeof(T) <= 4, "unsigned <= 32 bit");
        std::uint8_t b[sizeof(T)];
        Status st = writeRead(Bytes(&reg, 1), MutBytes(b, sizeof(T)));
        if (!ok(st))
            return st;
        T v = 0;
        for (std::size_t i = 0; i < sizeof(T); i++)
            v = static_cast<T>((v << 8) | b[i]);
        return v;
    }
    template <class T>
    Status writeReg(std::uint8_t reg, T v)
    {
        static_assert(std::is_unsigned<T>::value && sizeof(T) <= 4, "unsigned <= 32 bit");
        std::uint8_t b[1 + sizeof(T)];
        b[0] = reg;
        for (std::size_t i = 0; i < sizeof(T); i++)
            b[1 + i] = static_cast<std::uint8_t>(v >> (8 * (sizeof(T) - 1 - i)));
        return write(Bytes(b, sizeof(b)));
    }
    ep_i2c_device_t &raw() { return dev_; }

private:
    ep_i2c_device_t dev_{};
};

}  // namespace epp
