#include "ep/ep_common.h"
#include "ep/ep_crc.h"
#include "ep/ep_ringbuf.h"

#include <string.h>

const char *ep_status_str(ep_status_t s)
{
    switch (s) {
    case EP_OK: return "ok";
    case EP_ERR_PARAM: return "invalid parameter";
    case EP_ERR_TIMEOUT: return "timeout";
    case EP_ERR_NACK: return "nack";
    case EP_ERR_BUSY: return "busy";
    case EP_ERR_IO: return "i/o error";
    case EP_ERR_CRC: return "crc mismatch";
    case EP_ERR_OVERFLOW: return "overflow";
    case EP_ERR_UNSUPPORTED: return "unsupported";
    case EP_ERR_FRAME: return "frame error";
    case EP_ERR_ARBITRATION: return "arbitration lost";
    case EP_ERR_BUS_OFF: return "bus off";
    case EP_ERR_AGAIN: return "try again";
    case EP_ERR_STATE: return "invalid state";
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Ring buffer                                                         */
/* ------------------------------------------------------------------ */

ep_status_t ep_rb_init(ep_ringbuf_t *rb, uint8_t *storage, size_t size)
{
    if (!rb || !storage || size < 2 || (size & (size - 1)) != 0)
        return EP_ERR_PARAM;
    rb->buf = storage;
    rb->mask = size - 1;
    rb->head = 0;
    rb->tail = 0;
    return EP_OK;
}

void ep_rb_reset(ep_ringbuf_t *rb)
{
    EP_STORE_RELEASE(&rb->tail, EP_LOAD_ACQUIRE(&rb->head));
}

size_t ep_rb_count(const ep_ringbuf_t *rb)
{
    return EP_LOAD_ACQUIRE(&rb->head) - EP_LOAD_ACQUIRE(&rb->tail);
}

size_t ep_rb_space(const ep_ringbuf_t *rb)
{
    return rb->mask + 1 - ep_rb_count(rb);
}

bool ep_rb_put(ep_ringbuf_t *rb, uint8_t byte)
{
    size_t head = rb->head; /* producer owns head */
    if (head - EP_LOAD_ACQUIRE(&rb->tail) > rb->mask)
        return false;
    rb->buf[head & rb->mask] = byte;
    EP_STORE_RELEASE(&rb->head, head + 1);
    return true;
}

bool ep_rb_get(ep_ringbuf_t *rb, uint8_t *byte)
{
    size_t tail = rb->tail; /* consumer owns tail */
    if (tail == EP_LOAD_ACQUIRE(&rb->head))
        return false;
    *byte = rb->buf[tail & rb->mask];
    EP_STORE_RELEASE(&rb->tail, tail + 1);
    return true;
}

bool ep_rb_peek(const ep_ringbuf_t *rb, uint8_t *byte)
{
    size_t tail = rb->tail;
    if (tail == EP_LOAD_ACQUIRE(&rb->head))
        return false;
    *byte = rb->buf[tail & rb->mask];
    return true;
}

size_t ep_rb_write(ep_ringbuf_t *rb, const uint8_t *data, size_t len)
{
    size_t n = 0;
    while (n < len && ep_rb_put(rb, data[n]))
        n++;
    return n;
}

size_t ep_rb_read(ep_ringbuf_t *rb, uint8_t *data, size_t len)
{
    size_t n = 0;
    while (n < len && ep_rb_get(rb, &data[n]))
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* CRCs (bitwise: small code size, no tables in flash)                 */
/* ------------------------------------------------------------------ */

uint8_t ep_crc8_smbus(uint8_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

uint16_t ep_crc16_modbus(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (uint16_t)((crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1);
    }
    return crc;
}

uint16_t ep_crc16_ccitt(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= (uint16_t)(*data++ << 8);
        for (int i = 0; i < 8; i++)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

uint32_t ep_crc32_ieee(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}
