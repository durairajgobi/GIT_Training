/*
 * ep_framing.h - Packet framing for byte streams (UART / RS-232/422/485).
 *
 * Wire format:  COBS( payload || CRC16-CCITT(payload) big-endian ) 0x00
 *
 * COBS removes every 0x00 from the encoded data, so 0x00 is an
 * unambiguous frame delimiter: a receiver that starts mid-stream or
 * loses a byte resynchronises at the next delimiter. Overhead is at most
 * 1 byte per 254 plus the delimiter.
 */
#ifndef EP_FRAMING_H
#define EP_FRAMING_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_COBS_MAX_ENCODED(n) ((n) + ((n) / 254u) + 1u)

/* Return number of bytes written, or 0 on error (output too small). */
size_t ep_cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);
/* Input excludes the 0x00 delimiter. Returns decoded length, or -1. */
long ep_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

/* Encode a complete frame including CRC and trailing delimiter. */
size_t ep_frame_encode(const uint8_t *payload, size_t len, uint8_t *out, size_t cap);

/* Streaming decoder: feed received bytes one at a time. */
typedef struct {
    uint8_t *buf; /* raw (still COBS encoded) accumulation buffer */
    size_t cap;
    size_t len;
    bool overflow;
    uint32_t frames_ok, crc_errors, framing_errors;
} ep_frame_decoder_t;

void ep_frame_decoder_init(ep_frame_decoder_t *d, uint8_t *storage, size_t cap);
/* Returns EP_OK with *out_len set when a valid frame was completed,
 * EP_ERR_AGAIN while accumulating, EP_ERR_CRC / EP_ERR_FRAME /
 * EP_ERR_OVERFLOW for a bad frame (decoder already resynchronised). */
ep_status_t ep_frame_decoder_feed(ep_frame_decoder_t *d, uint8_t byte,
                                  uint8_t *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* EP_FRAMING_H */
