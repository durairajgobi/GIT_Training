#include "ep/ep_ptp.h"

#include <math.h>
#include <string.h>

const uint8_t EP_PTP_MAC_PRIMARY[6] = {0x01, 0x1B, 0x19, 0x00, 0x00, 0x00};
const uint8_t EP_PTP_MAC_PDELAY[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};

/* ------------------------------ Codec ------------------------------ */

void ep_ptp_ts_pack(uint8_t out[10], int64_t ns)
{
    if (ns < 0)
        ns = 0; /* PTP timestamps are unsigned */
    ep_put_be48(out, (uint64_t)(ns / EP_NS_PER_SEC));
    ep_put_be32(out + 6, (uint32_t)(ns % EP_NS_PER_SEC));
}

int64_t ep_ptp_ts_unpack(const uint8_t in[10])
{
    return (int64_t)ep_get_be48(in) * EP_NS_PER_SEC + ep_get_be32(in + 6);
}

uint16_t ep_ptp_msg_len(ep_ptp_msg_type_t t)
{
    switch (t) {
    case EP_PTP_SYNC:
    case EP_PTP_DELAY_REQ:
    case EP_PTP_FOLLOW_UP: return 44;
    case EP_PTP_PDELAY_REQ:
    case EP_PTP_PDELAY_RESP:
    case EP_PTP_DELAY_RESP:
    case EP_PTP_PDELAY_RESP_FUP: return 54;
    case EP_PTP_ANNOUNCE: return 64;
    case EP_PTP_SIGNALING: return 44;
    case EP_PTP_MANAGEMENT: return 48;
    }
    return 0;
}

bool ep_ptp_is_event(ep_ptp_msg_type_t t)
{
    return t <= EP_PTP_PDELAY_RESP;
}

static uint8_t control_field(uint8_t type)
{
    switch (type) {
    case EP_PTP_SYNC: return 0;
    case EP_PTP_DELAY_REQ: return 1;
    case EP_PTP_FOLLOW_UP: return 2;
    case EP_PTP_DELAY_RESP: return 3;
    case EP_PTP_MANAGEMENT: return 4;
    default: return 5;
    }
}

void ep_ptp_hdr_pack(uint8_t *b, const ep_ptp_hdr_t *h)
{
    b[0] = (uint8_t)((h->sdo_id_major << 4) | (h->msg_type & 0x0F));
    b[1] = (uint8_t)((h->minor_version << 4) | (h->version & 0x0F));
    ep_put_be16(b + 2, h->length);
    b[4] = h->domain;
    b[5] = h->sdo_id_minor;
    ep_put_be16(b + 6, h->flags);
    ep_put_be64(b + 8, (uint64_t)h->correction);
    ep_put_be32(b + 16, h->msg_specific);
    memcpy(b + 20, h->source.clock_id, 8);
    ep_put_be16(b + 28, h->source.port);
    ep_put_be16(b + 30, h->sequence_id);
    b[32] = h->control;
    b[33] = (uint8_t)h->log_interval;
}

ep_status_t ep_ptp_hdr_unpack(const uint8_t *b, size_t len, ep_ptp_hdr_t *h)
{
    if (!b || len < EP_PTP_HDR_LEN)
        return EP_ERR_FRAME;
    h->msg_type = b[0] & 0x0F;
    h->sdo_id_major = b[0] >> 4;
    h->version = b[1] & 0x0F;
    h->minor_version = b[1] >> 4;
    h->length = ep_get_be16(b + 2);
    h->domain = b[4];
    h->sdo_id_minor = b[5];
    h->flags = ep_get_be16(b + 6);
    h->correction = (int64_t)ep_get_be64(b + 8);
    h->msg_specific = ep_get_be32(b + 16);
    memcpy(h->source.clock_id, b + 20, 8);
    h->source.port = ep_get_be16(b + 28);
    h->sequence_id = ep_get_be16(b + 30);
    h->control = b[32];
    h->log_interval = (int8_t)b[33];
    if (h->version != 2)
        return EP_ERR_UNSUPPORTED;
    uint16_t need = ep_ptp_msg_len((ep_ptp_msg_type_t)h->msg_type);
    if (need == 0 || h->length < need || h->length > len)
        return EP_ERR_FRAME;
    return EP_OK;
}

static size_t prep(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, ep_ptp_msg_type_t type)
{
    uint16_t len = ep_ptp_msg_len(type);
    if (!buf || cap < len)
        return 0;
    memset(buf, 0, len);
    hdr->msg_type = (uint8_t)type;
    hdr->length = len;
    if (!hdr->version)
        hdr->version = 2;
    hdr->control = control_field((uint8_t)type);
    ep_ptp_hdr_pack(buf, hdr);
    return len;
}

size_t ep_ptp_build_ts_msg(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, ep_ptp_msg_type_t type,
                           int64_t ts_ns)
{
    if (type != EP_PTP_SYNC && type != EP_PTP_DELAY_REQ && type != EP_PTP_FOLLOW_UP &&
        type != EP_PTP_PDELAY_REQ)
        return 0;
    size_t len = prep(buf, cap, hdr, type);
    if (len)
        ep_ptp_ts_pack(buf + EP_PTP_HDR_LEN, ts_ns);
    return len;
}

size_t ep_ptp_build_resp(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, ep_ptp_msg_type_t type,
                         int64_t ts_ns, const ep_ptp_port_id_t *req)
{
    if (type != EP_PTP_DELAY_RESP && type != EP_PTP_PDELAY_RESP && type != EP_PTP_PDELAY_RESP_FUP)
        return 0;
    size_t len = prep(buf, cap, hdr, type);
    if (len) {
        ep_ptp_ts_pack(buf + EP_PTP_HDR_LEN, ts_ns);
        memcpy(buf + 44, req->clock_id, 8);
        ep_put_be16(buf + 52, req->port);
    }
    return len;
}

size_t ep_ptp_build_announce(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, const ep_ptp_announce_t *a)
{
    size_t len = prep(buf, cap, hdr, EP_PTP_ANNOUNCE);
    if (!len)
        return 0;
    uint8_t *p = buf + EP_PTP_HDR_LEN;
    ep_ptp_ts_pack(p, a->origin_ts);
    ep_put_be16(p + 10, (uint16_t)a->utc_offset);
    p[12] = 0;
    p[13] = a->gm_priority1;
    p[14] = a->gm_quality.clock_class;
    p[15] = a->gm_quality.accuracy;
    ep_put_be16(p + 16, a->gm_quality.variance);
    p[18] = a->gm_priority2;
    memcpy(p + 19, a->gm_identity, 8);
    ep_put_be16(p + 27, a->steps_removed);
    p[29] = a->time_source;
    return len;
}

int64_t ep_ptp_body_ts(const uint8_t *msg)
{
    return ep_ptp_ts_unpack(msg + EP_PTP_HDR_LEN);
}

void ep_ptp_body_port_id(const uint8_t *msg, ep_ptp_port_id_t *p)
{
    memcpy(p->clock_id, msg + 44, 8);
    p->port = ep_get_be16(msg + 52);
}

ep_status_t ep_ptp_parse_announce(const uint8_t *msg, size_t len, ep_ptp_announce_t *a)
{
    if (len < 64)
        return EP_ERR_FRAME;
    const uint8_t *p = msg + EP_PTP_HDR_LEN;
    a->origin_ts = ep_ptp_ts_unpack(p);
    a->utc_offset = (int16_t)ep_get_be16(p + 10);
    a->gm_priority1 = p[13];
    a->gm_quality.clock_class = p[14];
    a->gm_quality.accuracy = p[15];
    a->gm_quality.variance = ep_get_be16(p + 16);
    a->gm_priority2 = p[18];
    memcpy(a->gm_identity, p + 19, 8);
    a->steps_removed = ep_get_be16(p + 27);
    a->time_source = p[29];
    return EP_OK;
}

bool ep_ptp_port_id_equal(const ep_ptp_port_id_t *a, const ep_ptp_port_id_t *b)
{
    return a->port == b->port && memcmp(a->clock_id, b->clock_id, 8) == 0;
}

void ep_ptp_clock_id_from_mac(uint8_t id[8], const uint8_t mac[6])
{
    id[0] = mac[0];
    id[1] = mac[1];
    id[2] = mac[2];
    id[3] = 0xFF;
    id[4] = 0xFE;
    id[5] = mac[3];
    id[6] = mac[4];
    id[7] = mac[5];
}

/* --------------------------- Delay maths --------------------------- */

void ep_ptp_e2e(int64_t t1, int64_t t2, int64_t t3, int64_t t4,
                int64_t corr_ms, int64_t corr_sm, int64_t *offset, int64_t *mpd)
{
    int64_t ms = t2 - t1 - corr_ms; /* = delay + offset */
    int64_t sm = t4 - t3 - corr_sm; /* = delay - offset */
    int64_t d = (ms + sm) / 2;
    if (mpd)
        *mpd = d;
    if (offset)
        *offset = ms - d;
}

int64_t ep_ptp_p2p_link_delay(int64_t t1, int64_t t2, int64_t t3, int64_t t4,
                              int64_t corr_ns, double r)
{
    double turnaround_local = (double)(t4 - t1) * r;
    double residence_peer = (double)(t3 - t2);
    return (int64_t)llround((turnaround_local - residence_peer - (double)corr_ns) / 2.0);
}

double ep_ptp_rate_ratio(int64_t t3_old, int64_t t4_old, int64_t t3_new, int64_t t4_new)
{
    int64_t dl = t4_new - t4_old;
    if (dl <= 0)
        return 1.0;
    return (double)(t3_new - t3_old) / (double)dl;
}

/* ------------------------------- BMCA ------------------------------ */

static int cmp_u(unsigned a, unsigned b) { return (a > b) - (a < b); }

int ep_ptp_bmca_compare(const ep_ptp_dataset_t *a, const ep_ptp_dataset_t *b)
{
    int c = memcmp(a->gm_identity, b->gm_identity, 8);
    if (c != 0) {
        /* different grandmasters: compare their quality (Figure 27, part 1) */
        if ((c = cmp_u(a->priority1, b->priority1))) return c;
        if ((c = cmp_u(a->quality.clock_class, b->quality.clock_class))) return c;
        if ((c = cmp_u(a->quality.accuracy, b->quality.accuracy))) return c;
        if ((c = cmp_u(a->quality.variance, b->quality.variance))) return c;
        if ((c = cmp_u(a->priority2, b->priority2))) return c;
        return memcmp(a->gm_identity, b->gm_identity, 8) < 0 ? -1 : 1;
    }
    /* same grandmaster seen through different paths (part 2: topology) */
    if ((c = cmp_u(a->steps_removed, b->steps_removed))) return c;
    if ((c = memcmp(a->sender.clock_id, b->sender.clock_id, 8))) return c < 0 ? -1 : 1;
    if ((c = cmp_u(a->sender.port, b->sender.port))) return c;
    return cmp_u(a->receiver.port, b->receiver.port);
}

/* ------------------------------- Servo ----------------------------- */

void ep_ptp_servo_init(ep_ptp_servo_t *s, double kp, double ki, double max_ppb)
{
    memset(s, 0, sizeof(*s));
    s->kp = kp;
    s->ki = ki;
    s->max_ppb = max_ppb;
    s->first_step_threshold_ns = 20000; /* 20 us, linuxptp default */
    s->step_threshold_ns = 0;
}

void ep_ptp_servo_reset(ep_ptp_servo_t *s)
{
    s->count = 0;
}

static double clampd(double v, double m)
{
    return v > m ? m : v < -m ? -m : v;
}

double ep_ptp_servo_sample(ep_ptp_servo_t *s, int64_t offset, uint64_t local_ts,
                           ep_servo_state_t *state)
{
    double ppb = 0.0; /* internal sign: positive = local clock too fast */
    int64_t aoff = offset < 0 ? -offset : offset;

    switch (s->count) {
    case 0:
        s->offset[0] = offset;
        s->local[0] = local_ts;
        s->count = 1;
        *state = EP_SERVO_UNLOCKED;
        return -s->drift_ppb;
    case 1:
        s->offset[1] = offset;
        s->local[1] = local_ts;
        if (s->local[1] <= s->local[0]) {
            s->count = 0;
            *state = EP_SERVO_UNLOCKED;
            return -s->drift_ppb;
        }
        s->drift_ppb += (double)(s->offset[1] - s->offset[0]) * 1e9 /
                        (double)(s->local[1] - s->local[0]);
        s->drift_ppb = clampd(s->drift_ppb, s->max_ppb);
        if ((!s->first_update_done && s->first_step_threshold_ns && aoff > s->first_step_threshold_ns) ||
            (s->step_threshold_ns && aoff > s->step_threshold_ns))
            *state = EP_SERVO_JUMP;
        else
            *state = EP_SERVO_LOCKED;
        ppb = s->drift_ppb;
        s->count = 2;
        s->first_update_done = true;
        break;
    default:
        if (s->step_threshold_ns && aoff > s->step_threshold_ns) {
            s->count = 0;
            *state = EP_SERVO_UNLOCKED;
            return -s->drift_ppb;
        }
        {
            double ki_term = s->ki * (double)offset;
            ppb = s->kp * (double)offset + s->drift_ppb + ki_term;
            if (ppb < -s->max_ppb)
                ppb = -s->max_ppb;
            else if (ppb > s->max_ppb)
                ppb = s->max_ppb;
            else
                s->drift_ppb += ki_term; /* anti-windup: integrate only when not saturated */
        }
        *state = EP_SERVO_LOCKED;
        break;
    }
    return -ppb;
}

/* ------------------------ Ordinary clock (slave) ------------------- */

void ep_ptp_oc_config_default(ep_ptp_oc_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->self.port = 1;
    cfg->log_announce_interval = 1;
    cfg->announce_receipt_timeout = 3;
}

static void delay_filter_reset(ep_ptp_oc_t *oc)
{
    oc->n_delay = 0;
    oc->delay_idx = 0;
    oc->have_delay = false;
}

ep_status_t ep_ptp_oc_init(ep_ptp_oc_t *oc, const ep_ptp_oc_config_t *cfg)
{
    if (!oc || !cfg || !cfg->send || !cfg->clock.adj_freq || !cfg->clock.step)
        return EP_ERR_PARAM;
    memset(oc, 0, sizeof(*oc));
    oc->cfg = *cfg;
    oc->master_idx = -1;
    /* hardware timestamping defaults (cf. linuxptp pi.c) */
    ep_ptp_servo_init(&oc->servo, 0.7, 0.3, 900000.0); /* +/-900 ppm */
    oc->state = EP_PTP_PORT_LISTENING;
    return EP_OK;
}

static int64_t oc_now(const ep_ptp_oc_t *oc, int64_t fallback)
{
    return oc->cfg.clock.now ? oc->cfg.clock.now(oc->cfg.clock.ctx) : fallback;
}

static void select_master(ep_ptp_oc_t *oc, int64_t now)
{
    int best = -1;
    for (int i = 0; i < EP_PTP_FOREIGN_MAX; i++) {
        if (oc->foreign[i].announces < 2)
            continue; /* not qualified yet (1588: FOREIGN_MASTER_THRESHOLD) */
        if (best < 0 || ep_ptp_bmca_compare(&oc->foreign[i].ds, &oc->foreign[best].ds) < 0)
            best = i;
    }
    if (best != oc->master_idx) {
        oc->master_idx = best;
        oc->state = best < 0 ? EP_PTP_PORT_LISTENING : EP_PTP_PORT_UNCALIBRATED;
        oc->sync_pending = false;
        oc->dreq_pending = false;
        oc->last_announce_rx = now;
        ep_ptp_servo_reset(&oc->servo);
        delay_filter_reset(oc);
    }
}

static void handle_announce(ep_ptp_oc_t *oc, const ep_ptp_hdr_t *h, const uint8_t *msg, size_t len, int64_t now)
{
    ep_ptp_announce_t a;
    if (ep_ptp_parse_announce(msg, len, &a) != EP_OK)
        return;
    if (a.steps_removed >= 255)
        return;

    int idx = -1, free_idx = -1, oldest = 0;
    for (int i = 0; i < EP_PTP_FOREIGN_MAX; i++) {
        if (oc->foreign[i].announces && ep_ptp_port_id_equal(&oc->foreign[i].ds.sender, &h->source)) {
            idx = i;
            break;
        }
        if (!oc->foreign[i].announces && free_idx < 0)
            free_idx = i;
        if (oc->foreign[i].last_rx < oc->foreign[oldest].last_rx)
            oldest = i;
    }
    if (idx < 0) {
        idx = free_idx >= 0 ? free_idx : oldest;
        if (idx == oc->master_idx && free_idx < 0)
            return; /* never evict the current master for a newcomer */
        memset(&oc->foreign[idx], 0, sizeof(oc->foreign[idx]));
    }
    ep_ptp_foreign_t *f = &oc->foreign[idx];
    f->ds.priority1 = a.gm_priority1;
    f->ds.quality = a.gm_quality;
    f->ds.priority2 = a.gm_priority2;
    memcpy(f->ds.gm_identity, a.gm_identity, 8);
    f->ds.steps_removed = a.steps_removed;
    f->ds.sender = h->source;
    f->ds.receiver = oc->cfg.self;
    if (f->announces < 255)
        f->announces++;
    f->last_rx = now;

    if (idx == oc->master_idx)
        oc->last_announce_rx = now;
    select_master(oc, now);
}

static bool from_master(const ep_ptp_oc_t *oc, const ep_ptp_hdr_t *h)
{
    return oc->master_idx >= 0 &&
           ep_ptp_port_id_equal(&oc->foreign[oc->master_idx].ds.sender, &h->source);
}

static void send_delay_req(ep_ptp_oc_t *oc)
{
    uint8_t buf[64];
    ep_ptp_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.sdo_id_major = oc->cfg.sdo_id_major;
    h.version = 2;
    h.minor_version = 1;
    h.domain = oc->cfg.domain;
    h.source = oc->cfg.self;
    h.sequence_id = ++oc->dreq_seq;
    h.log_interval = 0x7F;
    size_t len = ep_ptp_build_ts_msg(buf, sizeof(buf), &h, EP_PTP_DELAY_REQ, 0);
    int64_t t3 = 0;
    if (oc->cfg.send(oc->cfg.send_arg, buf, len, true, &t3) == EP_OK) {
        oc->t3 = t3;
        oc->dreq_pending = true;
    }
}

static void process_sync(ep_ptp_oc_t *oc)
{
    oc->syncs++;
    if (oc->have_delay) {
        int64_t offset = (oc->t2 - oc->t1) - oc->corr_sync - oc->mean_path_delay;
        ep_servo_state_t st;
        double ppb = ep_ptp_servo_sample(&oc->servo, offset, (uint64_t)oc->t2, &st);
        oc->last_offset = offset;
        oc->servo_state = st;
        switch (st) {
        case EP_SERVO_UNLOCKED:
            break;
        case EP_SERVO_JUMP:
            oc->cfg.clock.step(oc->cfg.clock.ctx, -offset);
            oc->cfg.clock.adj_freq(oc->cfg.clock.ctx, ppb);
            oc->steps++;
            oc->dreq_pending = false; /* t3 taken before the step is void */
            oc->state = EP_PTP_PORT_UNCALIBRATED;
            oc->last_ppb = ppb;
            return; /* t1/t2 of this sync are also void */
        case EP_SERVO_LOCKED:
            oc->cfg.clock.adj_freq(oc->cfg.clock.ctx, ppb);
            oc->state = EP_PTP_PORT_SLAVE;
            break;
        }
        oc->last_ppb = ppb;
    }
    send_delay_req(oc);
}

static void handle_delay_resp(ep_ptp_oc_t *oc, const ep_ptp_hdr_t *h, const uint8_t *msg)
{
    ep_ptp_port_id_t req;
    ep_ptp_body_port_id(msg, &req);
    if (!oc->dreq_pending || h->sequence_id != oc->dreq_seq ||
        !ep_ptp_port_id_equal(&req, &oc->cfg.self))
        return;
    oc->dreq_pending = false;
    oc->delay_resps++;

    int64_t t4 = ep_ptp_body_ts(msg);
    int64_t mpd;
    ep_ptp_e2e(oc->t1, oc->t2, oc->t3, t4, oc->corr_sync, ep_ptp_corr_to_ns(h->correction),
               NULL, &mpd);
    if (mpd < 0)
        return; /* asymmetric glitch / clock stepped in between */

    oc->delay_samples[oc->delay_idx] = mpd;
    oc->delay_idx = (oc->delay_idx + 1) % EP_PTP_DELAY_FILTER;
    if (oc->n_delay < EP_PTP_DELAY_FILTER)
        oc->n_delay++;
    int64_t sum = 0;
    for (unsigned i = 0; i < oc->n_delay; i++)
        sum += oc->delay_samples[i];
    oc->mean_path_delay = sum / (int64_t)oc->n_delay;
    oc->have_delay = true;
}

void ep_ptp_oc_rx(ep_ptp_oc_t *oc, const uint8_t *msg, size_t len, int64_t rx_ts)
{
    ep_ptp_hdr_t h;
    if (ep_ptp_hdr_unpack(msg, len, &h) != EP_OK)
        return;
    if (h.domain != oc->cfg.domain || h.sdo_id_major != oc->cfg.sdo_id_major)
        return;
    if (memcmp(h.source.clock_id, oc->cfg.self.clock_id, 8) == 0)
        return; /* own message looped back */

    switch (h.msg_type) {
    case EP_PTP_ANNOUNCE:
        handle_announce(oc, &h, msg, len, oc_now(oc, rx_ts));
        break;
    case EP_PTP_SYNC:
        if (!from_master(oc, &h))
            return;
        if (h.flags & EP_PTP_FLAG_TWO_STEP) {
            /* hold t2 until Follow_Up so t1/t2 are always a matched pair */
            oc->sync_pending = true;
            oc->sync_seq = h.sequence_id;
            oc->pend_t2 = rx_ts;
            oc->pend_corr = ep_ptp_corr_to_ns(h.correction);
        } else {
            oc->sync_pending = false;
            oc->t1 = ep_ptp_body_ts(msg);
            oc->t2 = rx_ts;
            oc->corr_sync = ep_ptp_corr_to_ns(h.correction);
            process_sync(oc);
        }
        break;
    case EP_PTP_FOLLOW_UP:
        if (!from_master(oc, &h))
            return;
        if (!oc->sync_pending || h.sequence_id != oc->sync_seq) {
            oc->seq_errors++;
            return;
        }
        oc->sync_pending = false;
        oc->t1 = ep_ptp_body_ts(msg);
        oc->t2 = oc->pend_t2;
        oc->corr_sync = oc->pend_corr + ep_ptp_corr_to_ns(h.correction);
        process_sync(oc);
        break;
    case EP_PTP_DELAY_RESP:
        if (from_master(oc, &h))
            handle_delay_resp(oc, &h, msg);
        break;
    default:
        break;
    }
}

void ep_ptp_oc_poll(ep_ptp_oc_t *oc, int64_t now)
{
    if (oc->master_idx < 0)
        return;
    int8_t li = oc->cfg.log_announce_interval;
    int64_t interval = li >= 0 ? EP_NS_PER_SEC << li : EP_NS_PER_SEC >> -li;
    int64_t timeout = interval * (oc->cfg.announce_receipt_timeout ? oc->cfg.announce_receipt_timeout : 3);
    if (now - oc->last_announce_rx > timeout) {
        /* forget the silent master; select_master() then sees the change
         * and falls back to another qualified master or LISTENING */
        memset(&oc->foreign[oc->master_idx], 0, sizeof(oc->foreign[0]));
        select_master(oc, now);
    }
}
