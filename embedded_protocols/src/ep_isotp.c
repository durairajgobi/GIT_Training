#include "ep/ep_isotp.h"

#include <string.h>

enum { PCI_SF = 0, PCI_FF = 1, PCI_CF = 2, PCI_FC = 3 };
enum { FS_CTS = 0, FS_WAIT = 1, FS_OVFLW = 2 };

void ep_isotp_config_default(ep_isotp_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->padding = 0xCC;
    cfg->block_size = 8;
    cfg->st_min = 0;
    cfg->timeout_ms = 1000;
    cfg->max_wft = 10;
}

ep_status_t ep_isotp_init(ep_isotp_t *t, const ep_isotp_config_t *cfg)
{
    if (!t || !cfg || !cfg->send)
        return EP_ERR_PARAM;
    memset(t, 0, sizeof(*t));
    t->cfg = *cfg;
    if (!t->cfg.timeout_ms)
        t->cfg.timeout_ms = 1000;
    return EP_OK;
}

uint32_t ep_isotp_stmin_to_us(uint8_t raw)
{
    if (raw <= 0x7F)
        return (uint32_t)raw * 1000u;
    if (raw >= 0xF1 && raw <= 0xF9)
        return (uint32_t)(raw - 0xF0) * 100u;
    return 127000u; /* reserved values: use the maximum */
}

static uint64_t timeout_ns(const ep_isotp_t *t)
{
    return (uint64_t)t->cfg.timeout_ms * 1000000ull;
}

static ep_status_t tx_frame(ep_isotp_t *t, const uint8_t *data, size_t n)
{
    ep_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = t->cfg.tx_id;
    f.flags = t->cfg.ext_id ? EP_CAN_FLAG_EXT : 0;
    f.len = 8;
    memset(f.data, t->cfg.padding, 8);
    memcpy(f.data, data, n);
    return t->cfg.send(t->cfg.arg, &f);
}

static void send_fc(ep_isotp_t *t, uint8_t fs)
{
    uint8_t fc[3] = {(uint8_t)(0x30 | fs), t->cfg.block_size, t->cfg.st_min};
    tx_frame(t, fc, sizeof(fc));
}

static void tx_finish(ep_isotp_t *t, ep_status_t result)
{
    t->tx_state = EP_ISOTP_IDLE;
    if (result != EP_OK)
        t->errors++;
    if (t->cfg.on_tx_done)
        t->cfg.on_tx_done(t->cfg.arg, result);
}

static void tx_continue(ep_isotp_t *t, uint64_t now)
{
    while (t->tx_state == EP_ISOTP_SENDING && now >= t->tx_next_ns) {
        uint8_t cf[8];
        size_t n = EP_MIN((size_t)7, t->tx_len - t->tx_off);
        cf[0] = (uint8_t)(0x20 | t->tx_sn);
        memcpy(cf + 1, t->tx_buf + t->tx_off, n);
        if (tx_frame(t, cf, n + 1) != EP_OK)
            return; /* mailbox full: retry on next poll */
        t->tx_off += n;
        t->tx_sn = (uint8_t)((t->tx_sn + 1) & 0x0F);
        if (t->tx_off >= t->tx_len) {
            tx_finish(t, EP_OK);
            return;
        }
        if (t->tx_bs && --t->tx_bs_left == 0) {
            t->tx_state = EP_ISOTP_WAIT_FC;
            t->tx_deadline_ns = now + timeout_ns(t);
            return;
        }
        t->tx_next_ns = now + (uint64_t)t->tx_stmin_us * 1000u;
        if (t->tx_stmin_us)
            return; /* honour STmin: next CF from poll() */
    }
}

ep_status_t ep_isotp_send(ep_isotp_t *t, const uint8_t *data, size_t len, uint64_t now)
{
    if (!data || len == 0 || len > EP_ISOTP_MAX_LEN)
        return EP_ERR_PARAM;
    if (t->tx_state != EP_ISOTP_IDLE)
        return EP_ERR_BUSY;

    if (len <= 7) {
        uint8_t sf[8];
        sf[0] = (uint8_t)len;
        memcpy(sf + 1, data, len);
        ep_status_t st = tx_frame(t, sf, len + 1);
        if (st == EP_OK && t->cfg.on_tx_done)
            t->cfg.on_tx_done(t->cfg.arg, EP_OK);
        return st;
    }

    uint8_t ff[8];
    ff[0] = (uint8_t)(0x10 | ((len >> 8) & 0x0F));
    ff[1] = (uint8_t)len;
    memcpy(ff + 2, data, 6);
    ep_status_t st = tx_frame(t, ff, 8);
    if (st != EP_OK)
        return st;
    t->tx_buf = data;
    t->tx_len = len;
    t->tx_off = 6;
    t->tx_sn = 1;
    t->tx_wft = 0;
    t->tx_state = EP_ISOTP_WAIT_FC;
    t->tx_deadline_ns = now + timeout_ns(t);
    return EP_OK;
}

static void rx_abort(ep_isotp_t *t)
{
    t->rx_state = EP_ISOTP_IDLE;
    t->errors++;
}

void ep_isotp_on_frame(ep_isotp_t *t, const ep_can_frame_t *f, uint64_t now)
{
    bool ext = (f->flags & EP_CAN_FLAG_EXT) != 0;
    if (f->id != t->cfg.rx_id || ext != t->cfg.ext_id || f->len < 1 ||
        (f->flags & EP_CAN_FLAG_RTR))
        return;
    const uint8_t *d = f->data;

    switch (d[0] >> 4) {
    case PCI_SF: {
        size_t sl = d[0] & 0x0F;
        if (sl == 0 || sl > 7 || sl + 1 > f->len || sl > t->cfg.rx_cap) {
            t->errors++;
            return;
        }
        if (t->rx_state == EP_ISOTP_RECEIVING)
            rx_abort(t); /* SF interrupts a running reception */
        memcpy(t->cfg.rx_buf, d + 1, sl);
        if (t->cfg.on_rx)
            t->cfg.on_rx(t->cfg.arg, t->cfg.rx_buf, sl);
        break;
    }
    case PCI_FF: {
        if (f->len < 8)
            return;
        size_t flen = ((size_t)(d[0] & 0x0F) << 8) | d[1];
        if (flen <= 7)
            return; /* must have used SF */
        if (t->rx_state == EP_ISOTP_RECEIVING)
            rx_abort(t);
        if (flen > t->cfg.rx_cap) {
            send_fc(t, FS_OVFLW);
            t->errors++;
            return;
        }
        memcpy(t->cfg.rx_buf, d + 2, 6);
        t->rx_len = flen;
        t->rx_off = 6;
        t->rx_sn = 1;
        t->rx_bs_cnt = 0;
        t->rx_state = EP_ISOTP_RECEIVING;
        t->rx_deadline_ns = now + timeout_ns(t);
        send_fc(t, FS_CTS);
        break;
    }
    case PCI_CF: {
        if (t->rx_state != EP_ISOTP_RECEIVING)
            return;
        if ((d[0] & 0x0F) != t->rx_sn) {
            rx_abort(t); /* lost or duplicated frame */
            return;
        }
        size_t n = EP_MIN((size_t)7, t->rx_len - t->rx_off);
        if ((size_t)f->len < n + 1) {
            rx_abort(t);
            return;
        }
        memcpy(t->cfg.rx_buf + t->rx_off, d + 1, n);
        t->rx_off += n;
        t->rx_sn = (uint8_t)((t->rx_sn + 1) & 0x0F);
        t->rx_deadline_ns = now + timeout_ns(t);
        if (t->rx_off >= t->rx_len) {
            t->rx_state = EP_ISOTP_IDLE;
            if (t->cfg.on_rx)
                t->cfg.on_rx(t->cfg.arg, t->cfg.rx_buf, t->rx_len);
        } else if (t->cfg.block_size && ++t->rx_bs_cnt >= t->cfg.block_size) {
            t->rx_bs_cnt = 0;
            send_fc(t, FS_CTS);
        }
        break;
    }
    case PCI_FC: {
        if (t->tx_state != EP_ISOTP_WAIT_FC || f->len < 3)
            return;
        switch (d[0] & 0x0F) {
        case FS_CTS:
            t->tx_bs = d[1];
            t->tx_bs_left = d[1];
            t->tx_stmin_us = ep_isotp_stmin_to_us(d[2]);
            t->tx_state = EP_ISOTP_SENDING;
            t->tx_next_ns = now;
            tx_continue(t, now);
            break;
        case FS_WAIT:
            if (++t->tx_wft > t->cfg.max_wft)
                tx_finish(t, EP_ERR_TIMEOUT);
            else
                t->tx_deadline_ns = now + timeout_ns(t);
            break;
        case FS_OVFLW:
            tx_finish(t, EP_ERR_OVERFLOW);
            break;
        default:
            tx_finish(t, EP_ERR_FRAME);
            break;
        }
        break;
    }
    default:
        break;
    }
}

void ep_isotp_poll(ep_isotp_t *t, uint64_t now)
{
    if (t->tx_state == EP_ISOTP_WAIT_FC && now >= t->tx_deadline_ns)
        tx_finish(t, EP_ERR_TIMEOUT); /* N_Bs */
    else if (t->tx_state == EP_ISOTP_SENDING)
        tx_continue(t, now);
    if (t->rx_state == EP_ISOTP_RECEIVING && now >= t->rx_deadline_ns)
        rx_abort(t); /* N_Cr */
}
