/*
 * ep_ringbuf.h - Lock-free single-producer / single-consumer byte FIFO.
 *
 * One side (typically an ISR) only calls put/write, the other side only
 * calls get/read. No critical section is needed on any core that has
 * naturally atomic word-sized loads/stores. Size must be a power of two;
 * all `size` bytes are usable (head/tail are free-running counters).
 */
#ifndef EP_RINGBUF_H
#define EP_RINGBUF_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;
    size_t mask;
    size_t head; /* written by producer only */
    size_t tail; /* written by consumer only */
} ep_ringbuf_t;

ep_status_t ep_rb_init(ep_ringbuf_t *rb, uint8_t *storage, size_t size);
void ep_rb_reset(ep_ringbuf_t *rb);
size_t ep_rb_count(const ep_ringbuf_t *rb);
size_t ep_rb_space(const ep_ringbuf_t *rb);
bool ep_rb_put(ep_ringbuf_t *rb, uint8_t byte);
bool ep_rb_get(ep_ringbuf_t *rb, uint8_t *byte);
bool ep_rb_peek(const ep_ringbuf_t *rb, uint8_t *byte);
size_t ep_rb_write(ep_ringbuf_t *rb, const uint8_t *data, size_t len);
size_t ep_rb_read(ep_ringbuf_t *rb, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EP_RINGBUF_H */
