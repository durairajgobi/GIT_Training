/*
 * ep_common.h - Common types for the embedded protocol stack.
 *
 * Design rules for every module in this library:
 *   - No dynamic allocation. All storage is provided by the caller.
 *   - No OS dependency. Hardware is reached through small "ops" tables
 *     (function pointers + opaque context) that a board port fills in.
 *   - ISR-safe hand-off between interrupt and task context via the
 *     single-producer/single-consumer ring buffer (ep_ringbuf.h).
 *   - Every public function returns ep_status_t (or a value + status).
 */
#ifndef EP_COMMON_H
#define EP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EP_OK               = 0,
    EP_ERR_PARAM        = -1,  /* invalid argument                        */
    EP_ERR_TIMEOUT      = -2,  /* operation did not complete in time      */
    EP_ERR_NACK         = -3,  /* I2C address/data not acknowledged       */
    EP_ERR_BUSY         = -4,  /* resource busy (mailbox full, tx active) */
    EP_ERR_IO           = -5,  /* low-level bus/driver error              */
    EP_ERR_CRC          = -6,  /* checksum / CRC / PEC mismatch           */
    EP_ERR_OVERFLOW     = -7,  /* buffer too small / queue overflow       */
    EP_ERR_UNSUPPORTED  = -8,  /* feature not supported by this backend   */
    EP_ERR_FRAME        = -9,  /* malformed frame / protocol violation    */
    EP_ERR_ARBITRATION  = -10, /* bus arbitration lost (I2C multi-master) */
    EP_ERR_BUS_OFF      = -11, /* CAN controller in bus-off               */
    EP_ERR_AGAIN        = -12, /* nothing available right now, retry      */
    EP_ERR_STATE        = -13  /* call not valid in the current state     */
} ep_status_t;

const char *ep_status_str(ep_status_t s);

#define EP_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define EP_MIN(a, b)     ((a) < (b) ? (a) : (b))
#define EP_MAX(a, b)     ((a) > (b) ? (a) : (b))
#define EP_NS_PER_SEC    1000000000LL
#define EP_NS_PER_US     1000LL

/* ------------------------------------------------------------------ */
/* Memory ordering for ISR <-> task hand-off.                          */
/* GCC/Clang builtins work on Cortex-M, Cortex-A, RISC-V and x86. A    */
/* toolchain without them can override these macros before including. */
/* ------------------------------------------------------------------ */
#ifndef EP_LOAD_ACQUIRE
#define EP_LOAD_ACQUIRE(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define EP_STORE_RELEASE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#endif

/* ------------------------------------------------------------------ */
/* Byte-order helpers (wire formats are big-endian unless noted).      */
/* ------------------------------------------------------------------ */
static inline uint16_t ep_get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static inline uint32_t ep_get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t ep_get_be48(const uint8_t *p)
{
    return ((uint64_t)ep_get_be16(p) << 32) | ep_get_be32(p + 2);
}
static inline uint64_t ep_get_be64(const uint8_t *p)
{
    return ((uint64_t)ep_get_be32(p) << 32) | ep_get_be32(p + 4);
}
static inline void ep_put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static inline void ep_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static inline void ep_put_be48(uint8_t *p, uint64_t v)
{
    ep_put_be16(p, (uint16_t)(v >> 32));
    ep_put_be32(p + 2, (uint32_t)v);
}
static inline void ep_put_be64(uint8_t *p, uint64_t v)
{
    ep_put_be32(p, (uint32_t)(v >> 32));
    ep_put_be32(p + 4, (uint32_t)v);
}
static inline uint16_t ep_get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline void ep_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* ------------------------------------------------------------------ */
/* Board services every port provides.                                 */
/* ------------------------------------------------------------------ */

/* Monotonic time base and busy-wait delay. */
typedef struct {
    uint64_t (*now_ns)(void *ctx);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} ep_clock_t;

static inline uint64_t ep_clock_now(const ep_clock_t *c)
{
    return (c && c->now_ns) ? c->now_ns(c->ctx) : 0;
}
static inline void ep_clock_delay_us(const ep_clock_t *c, uint32_t us)
{
    if (c && c->delay_us && us)
        c->delay_us(c->ctx, us);
}

/* Digital I/O. For open-drain lines (I2C) write(1) releases the line. */
typedef struct {
    void (*write)(void *ctx, uint32_t pin, bool level);
    bool (*read)(void *ctx, uint32_t pin);
    void *ctx;
} ep_gpio_t;

#define EP_PIN_NONE 0xFFFFFFFFu

static inline void ep_gpio_write(const ep_gpio_t *g, uint32_t pin, bool level)
{
    if (g && g->write && pin != EP_PIN_NONE)
        g->write(g->ctx, pin, level);
}
static inline bool ep_gpio_read(const ep_gpio_t *g, uint32_t pin)
{
    return (g && g->read && pin != EP_PIN_NONE) ? g->read(g->ctx, pin) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* EP_COMMON_H */
