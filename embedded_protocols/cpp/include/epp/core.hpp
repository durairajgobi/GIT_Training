// core.hpp - C++17 vocabulary types for the embedded protocol stack.
//
// Built for -fno-exceptions -fno-rtti firmware: errors are values
// (Status / Result<T>), buffers are non-owning Span<T>, containers have a
// compile-time capacity and never allocate.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "ep/ep_common.h"

namespace epp {

enum class Status : int {
    Ok = EP_OK,
    Param = EP_ERR_PARAM,
    Timeout = EP_ERR_TIMEOUT,
    Nack = EP_ERR_NACK,
    Busy = EP_ERR_BUSY,
    Io = EP_ERR_IO,
    Crc = EP_ERR_CRC,
    Overflow = EP_ERR_OVERFLOW,
    Unsupported = EP_ERR_UNSUPPORTED,
    Frame = EP_ERR_FRAME,
    Arbitration = EP_ERR_ARBITRATION,
    BusOff = EP_ERR_BUS_OFF,
    Again = EP_ERR_AGAIN,
    State = EP_ERR_STATE,
};

constexpr Status toStatus(ep_status_t s) { return static_cast<Status>(static_cast<int>(s)); }
constexpr bool ok(Status s) { return s == Status::Ok; }
inline const char *toString(Status s) { return ep_status_str(static_cast<ep_status_t>(s)); }

// Propagate a non-Ok status to the caller.
#define EPP_TRY(expr)                                                  \
    do {                                                               \
        ::epp::Status _st = (expr);                                    \
        if (_st != ::epp::Status::Ok)                                  \
            return _st;                                                \
    } while (0)

template <class T>
class Result {
public:
    constexpr Result(T v) : value_(v), status_(Status::Ok) {}           // NOLINT
    constexpr Result(Status s) : value_(), status_(s) {}               // NOLINT
    constexpr bool ok() const { return status_ == Status::Ok; }
    constexpr explicit operator bool() const { return ok(); }
    constexpr Status status() const { return status_; }
    constexpr const T &value() const { return value_; }
    constexpr T valueOr(T fallback) const { return ok() ? value_ : fallback; }

private:
    T value_;
    Status status_;
};

// Minimal std::span stand-in (C++17).
template <class T>
class Span {
public:
    constexpr Span() = default;
    constexpr Span(T *p, std::size_t n) : p_(p), n_(n) {}
    template <std::size_t N>
    constexpr Span(T (&a)[N]) : p_(a), n_(N) {}                         // NOLINT
    template <class U, std::size_t N, class = std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value>>
    constexpr Span(std::array<U, N> &a) : p_(a.data()), n_(N) {}       // NOLINT
    template <class U, std::size_t N, class = std::enable_if_t<std::is_convertible<const U (*)[], T (*)[]>::value>>
    constexpr Span(const std::array<U, N> &a) : p_(a.data()), n_(N) {} // NOLINT
    template <class U, class = std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value>>
    constexpr Span(const Span<U> &o) : p_(o.data()), n_(o.size()) {}  // NOLINT

    constexpr T *data() const { return p_; }
    constexpr std::size_t size() const { return n_; }
    constexpr bool empty() const { return n_ == 0; }
    constexpr T &operator[](std::size_t i) const { return p_[i]; }
    constexpr T *begin() const { return p_; }
    constexpr T *end() const { return p_ + n_; }
    constexpr Span first(std::size_t n) const { return {p_, n < n_ ? n : n_}; }
    constexpr Span subspan(std::size_t off) const { return off < n_ ? Span{p_ + off, n_ - off} : Span{}; }

private:
    T *p_ = nullptr;
    std::size_t n_ = 0;
};

using Bytes = Span<const std::uint8_t>;
using MutBytes = Span<std::uint8_t>;

// Lock-free single-producer/single-consumer queue of trivially copyable T.
// N must be a power of two; all N slots are usable.
template <class T, std::size_t N>
class RingBuffer {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

public:
    bool push(const T &v)
    {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= N)
            return false;
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T &out)
    {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false;
        out = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    std::size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
    bool empty() const { return size() == 0; }
    bool full() const { return size() >= N; }
    static constexpr std::size_t capacity() { return N; }

private:
    std::array<T, N> buf_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

}  // namespace epp
