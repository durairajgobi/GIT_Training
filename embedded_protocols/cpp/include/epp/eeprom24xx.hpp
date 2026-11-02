// eeprom24xx.hpp - I2C serial EEPROM (24C02 ... 24C512, M24xx, AT24xx).
//
// Handles the two things that make these parts tricky:
//   * Page writes wrap inside the page: a write must never cross a page
//     boundary, so data is split at page boundaries.
//   * Internal write cycle (tWR, up to 5 ms): the chip NACKs its address
//     until done. "ACK polling" waits exactly as long as needed.
#pragma once

#include <cstring>

#include "epp/core.hpp"

namespace epp {

template <class Dev>
class Eeprom24xx {
public:
    struct Geometry {
        std::size_t size;      // bytes
        std::size_t page;      // page size in bytes
        std::uint8_t addr_len; // 1 (<= 24C16) or 2
    };
    static constexpr Geometry k24c02{256, 8, 1};
    static constexpr Geometry k24c32{4096, 32, 2};
    static constexpr Geometry k24c256{32768, 64, 2};

    // poll_delay: called between ACK polls (e.g. 100 us busy-wait / RTOS sleep)
    Eeprom24xx(Dev &dev, Geometry g, void (*poll_delay)() = nullptr, unsigned max_polls = 100)
        : dev_(dev), g_(g), poll_delay_(poll_delay), max_polls_(max_polls)
    {
    }

    Status read(std::size_t addr, MutBytes out)
    {
        if (addr + out.size() > g_.size)
            return Status::Param;
        std::uint8_t a[2];
        std::size_t al = encodeAddr(addr, a);
        return dev_.writeRead(Bytes(a, al), out); // sequential read crosses pages freely
    }

    Status write(std::size_t addr, Bytes data)
    {
        if (addr + data.size() > g_.size)
            return Status::Param;
        std::size_t off = 0;
        while (off < data.size()) {
            std::size_t room = g_.page - ((addr + off) % g_.page);
            std::size_t n = data.size() - off < room ? data.size() - off : room;
            std::uint8_t buf[2 + 64];
            if (n > 64)
                return Status::Param;
            std::size_t al = encodeAddr(addr + off, buf);
            std::memcpy(buf + al, data.data() + off, n);
            EPP_TRY(dev_.write(Bytes(buf, al + n)));
            EPP_TRY(waitReady());
            off += n;
            pages_written_++;
        }
        return Status::Ok;
    }

    Status waitReady()
    {
        for (unsigned i = 0; i < max_polls_; i++) {
            Status st = dev_.write(Bytes()); // address-only probe
            if (st == Status::Ok)
                return Status::Ok;
            if (st != Status::Nack)
                return st;
            if (poll_delay_)
                poll_delay_();
        }
        return Status::Timeout;
    }

    unsigned pagesWritten() const { return pages_written_; }

private:
    std::size_t encodeAddr(std::size_t addr, std::uint8_t *o) const
    {
        if (g_.addr_len == 2) {
            o[0] = static_cast<std::uint8_t>(addr >> 8);
            o[1] = static_cast<std::uint8_t>(addr);
            return 2;
        }
        o[0] = static_cast<std::uint8_t>(addr);
        return 1;
    }

    Dev &dev_;
    Geometry g_;
    void (*poll_delay_)();
    unsigned max_polls_;
    unsigned pages_written_ = 0;
};

}  // namespace epp
