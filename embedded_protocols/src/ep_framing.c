#include "ep/ep_framing.h"
#include "ep/ep_crc.h"

/* Incremental COBS encoder shared by ep_cobs_encode and ep_frame_encode. */
typedef struct {
    uint8_t *out;
    size_t cap, o, code_idx;
    uint8_t code;
    bool err;
} cobs_enc_t;

static void enc_begin(cobs_enc_t *e, uint8_t *out, size_t cap)
{
    e->out = out;
    e->cap = cap;
    e->code_idx = 0;
    e->o = 1;
    e->code = 1;
    e->err = cap < 1;
}

static void enc_close_block(cobs_enc_t *e)
{
    if (e->code_idx >= e->cap) {
        e->err = true;
        return;
    }
    e->out[e->code_idx] = e->code;
    e->code_idx = e->o++;
    e->code = 1;
}

/* `more` = further input bytes follow (keeps the output canonical). */
static void enc_put(cobs_enc_t *e, uint8_t b, bool more)
{
    if (e->err)
        return;
    if (b == 0) {
        enc_close_block(e);
        return;
    }
    if (e->o >= e->cap) {
        e->err = true;
        return;
    }
    e->out[e->o++] = b;
    if (++e->code == 0xFF && more)
        enc_close_block(e);
}

static size_t enc_end(cobs_enc_t *e)
{
    if (e->err || e->code_idx >= e->cap)
        return 0;
    e->out[e->code_idx] = e->code;
    return e->o;
}

size_t ep_cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    cobs_enc_t e;
    enc_begin(&e, out, cap);
    for (size_t i = 0; i < len; i++)
        enc_put(&e, in[i], i + 1 < len);
    return enc_end(&e);
}

long ep_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    size_t i = 0, o = 0;
    while (i < len) {
        uint8_t code = in[i++];
        if (code == 0)
            return -1;
        for (uint8_t k = 1; k < code; k++) {
            if (i >= len || in[i] == 0 || o >= cap)
                return -1;
            out[o++] = in[i++];
        }
        if (code != 0xFF && i < len) {
            if (o >= cap)
                return -1;
            out[o++] = 0;
        }
    }
    return (long)o;
}

size_t ep_frame_encode(const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    /* COBS is a streaming code, so payload and CRC are encoded as two
     * chunks in one pass: no temporary payload||crc copy is needed. */
    uint8_t crcb[2];
    ep_put_be16(crcb, ep_crc16_ccitt(EP_CRC16_CCITT_INIT, payload, len));

    cobs_enc_t e;
    enc_begin(&e, out, cap);
    for (size_t i = 0; i < len; i++)
        enc_put(&e, payload[i], true);
    enc_put(&e, crcb[0], true);
    enc_put(&e, crcb[1], false);
    size_t n = enc_end(&e);
    if (n == 0 || n >= cap)
        return 0;
    out[n++] = 0x00; /* delimiter */
    return n;
}

void ep_frame_decoder_init(ep_frame_decoder_t *d, uint8_t *storage, size_t cap)
{
    d->buf = storage;
    d->cap = cap;
    d->len = 0;
    d->overflow = false;
    d->frames_ok = d->crc_errors = d->framing_errors = 0;
}

ep_status_t ep_frame_decoder_feed(ep_frame_decoder_t *d, uint8_t byte,
                                  uint8_t *out, size_t cap, size_t *out_len)
{
    if (byte != 0) {
        if (d->len < d->cap)
            d->buf[d->len++] = byte;
        else
            d->overflow = true;
        return EP_ERR_AGAIN;
    }

    /* delimiter */
    size_t raw = d->len;
    bool ovf = d->overflow;
    d->len = 0;
    d->overflow = false;
    if (raw == 0)
        return EP_ERR_AGAIN; /* back-to-back delimiters (idle fill) */
    if (ovf) {
        d->framing_errors++;
        return EP_ERR_OVERFLOW;
    }
    long n = ep_cobs_decode(d->buf, raw, out, cap);
    if (n < 2) {
        d->framing_errors++;
        return EP_ERR_FRAME;
    }
    size_t plen = (size_t)n - 2;
    if (ep_crc16_ccitt(EP_CRC16_CCITT_INIT, out, plen) != ep_get_be16(out + plen)) {
        d->crc_errors++;
        return EP_ERR_CRC;
    }
    d->frames_ok++;
    *out_len = plen;
    return EP_OK;
}
