#include "ep/ep_tsn.h"
#include "ep/ep_eth.h"

#include <stdio.h>
#include <string.h>

/* --------------------------- 802.1Qbv TAS -------------------------- */

ep_status_t ep_tsn_tas_init(ep_tsn_tas_t *tas, const ep_tsn_gcl_entry_t *gcl, size_t n,
                            uint64_t base, uint64_t cycle, uint64_t link_bps)
{
    if (!tas || !gcl || n == 0 || !link_bps)
        return EP_ERR_PARAM;
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        if (gcl[i].interval_ns == 0 || gcl[i].op > EP_GCL_SET_AND_RELEASE)
            return EP_ERR_PARAM;
        sum += gcl[i].interval_ns;
    }
    tas->gcl = gcl;
    tas->n = n;
    tas->base_time_ns = base;
    tas->cycle_time_ns = cycle ? cycle : sum;
    tas->link_bps = link_bps;
    return EP_OK;
}

uint64_t ep_tsn_tas_next_cycle_start(const ep_tsn_tas_t *tas, uint64_t now)
{
    if (now <= tas->base_time_ns)
        return tas->base_time_ns;
    uint64_t k = (now - tas->base_time_ns + tas->cycle_time_ns - 1) / tas->cycle_time_ns;
    return tas->base_time_ns + k * tas->cycle_time_ns;
}

uint8_t ep_tsn_tas_gates(const ep_tsn_tas_t *tas, uint64_t now, uint64_t *entry_end, size_t *idx)
{
    if (now < tas->base_time_ns) {
        /* schedule not started yet: all gates open until base time */
        if (entry_end) *entry_end = tas->base_time_ns;
        if (idx) *idx = SIZE_MAX;
        return 0xFF;
    }
    const uint64_t cycle = tas->cycle_time_ns;
    uint64_t cs = tas->base_time_ns + ((now - tas->base_time_ns) / cycle) * cycle;
    uint64_t off = now - cs, start = 0;
    for (size_t i = 0; i < tas->n; i++) {
        uint64_t end = start + tas->gcl[i].interval_ns;
        if (i == tas->n - 1 && end < cycle)
            end = cycle; /* last entry extended to end of cycle */
        if (end > cycle)
            end = cycle; /* truncated */
        if (off < end) {
            if (entry_end) *entry_end = cs + end;
            if (idx) *idx = i;
            return tas->gcl[i].gate_mask;
        }
        start = end;
    }
    /* unreachable: off < cycle always lands in some entry */
    if (entry_end) *entry_end = cs + cycle;
    if (idx) *idx = tas->n - 1;
    return tas->gcl[tas->n - 1].gate_mask;
}

bool ep_tsn_tas_gate_open(const ep_tsn_tas_t *tas, uint64_t now, uint8_t tc, uint64_t *close_at)
{
    uint64_t end;
    uint8_t mask = ep_tsn_tas_gates(tas, now, &end, NULL);
    if (!(mask & (1u << tc)))
        return false;
    /* merge consecutive open entries; one full cycle of "open" = never closes */
    for (size_t steps = 0; steps <= tas->n + 1; steps++) {
        uint64_t next_end;
        mask = ep_tsn_tas_gates(tas, end, &next_end, NULL);
        if (!(mask & (1u << tc))) {
            if (close_at) *close_at = end;
            return true;
        }
        end = next_end;
    }
    if (close_at) *close_at = EP_TSN_NEVER;
    return true;
}

bool ep_tsn_tas_can_tx(const ep_tsn_tas_t *tas, uint64_t now, uint8_t tc, size_t frame_len)
{
    uint64_t close_at;
    if (tc >= EP_TSN_NUM_TC || !ep_tsn_tas_gate_open(tas, now, tc, &close_at))
        return false;
    if (close_at == EP_TSN_NEVER)
        return true;
    return now + ep_eth_wire_time_ns(frame_len, tas->link_bps) <= close_at;
}

uint64_t ep_tsn_tas_next_tx(const ep_tsn_tas_t *tas, uint64_t now, uint8_t tc, size_t frame_len)
{
    uint64_t t = now;
    /* examine every GCL transition over two cycles (+ the pre-base window) */
    for (size_t i = 0; i < 2 * tas->n + 2; i++) {
        if (ep_tsn_tas_can_tx(tas, t, tc, frame_len))
            return t;
        uint64_t end;
        ep_tsn_tas_gates(tas, t, &end, NULL);
        t = end;
    }
    return EP_TSN_NEVER;
}

int ep_tsn_taprio_cmd(const ep_tsn_tas_t *tas, const char *ifname, uint8_t num_tc,
                      const uint8_t map[16], bool hw_offload, char *buf, size_t cap)
{
    size_t o = 0;
    int n;
#define APPEND(...)                                                          \
    do {                                                                     \
        n = snprintf(buf ? buf + EP_MIN(o, cap) : NULL,                      \
                     o < cap ? cap - o : 0, __VA_ARGS__);                    \
        if (n < 0)                                                           \
            return -1;                                                       \
        o += (size_t)n;                                                      \
    } while (0)

    APPEND("tc qdisc replace dev %s parent root handle 100 taprio num_tc %u map", ifname, num_tc);
    for (int i = 0; i < 16; i++)
        APPEND(" %u", map[i]);
    APPEND(" queues");
    for (unsigned q = 0; q < num_tc; q++)
        APPEND(" 1@%u", q);
    APPEND(" base-time %llu", (unsigned long long)tas->base_time_ns);
    for (size_t i = 0; i < tas->n; i++) {
        const char *cmd = tas->gcl[i].op == EP_GCL_SET_AND_HOLD      ? "H"
                          : tas->gcl[i].op == EP_GCL_SET_AND_RELEASE ? "R"
                                                                     : "S";
        APPEND(" sched-entry %s %02x %u", cmd, tas->gcl[i].gate_mask, tas->gcl[i].interval_ns);
    }
    APPEND(" cycle-time %llu", (unsigned long long)tas->cycle_time_ns);
    if (hw_offload)
        APPEND(" flags 0x2");
    else
        APPEND(" clockid CLOCK_TAI");
#undef APPEND
    return (int)o;
}

/* --------------------------- 802.1Qav CBS -------------------------- */

ep_status_t ep_tsn_cbs_init(ep_tsn_cbs_t *c, uint64_t idle_slope, uint64_t port_bps,
                            uint32_t max_frame_bytes, uint32_t max_interference_bytes)
{
    if (!c || !port_bps || idle_slope == 0 || idle_slope >= port_bps)
        return EP_ERR_PARAM;
    memset(c, 0, sizeof(*c));
    c->idle_slope_bps = (int64_t)idle_slope;
    c->send_slope_bps = (int64_t)idle_slope - (int64_t)port_bps;
    c->port_bps = port_bps;
    /* 802.1Q Annex L: hiCredit = maxInterferenceSize * idleSlope / portRate
     *                 loCredit = maxFrameSize * sendSlope / portRate      */
    c->hi_credit_bits = (int64_t)((uint64_t)max_interference_bytes * 8u * idle_slope / port_bps);
    c->lo_credit_bits = (int64_t)max_frame_bytes * 8 * c->send_slope_bps / (int64_t)port_bps;
    return EP_OK;
}

void ep_tsn_cbs_update(ep_tsn_cbs_t *c, uint64_t now, bool queue_nonempty)
{
    if (now <= c->last_ns) {
        if (!queue_nonempty && c->credit_nbits > 0)
            c->credit_nbits = 0;
        return;
    }
    uint64_t dt = now - c->last_ns;
    c->last_ns = now;
    if (queue_nonempty || c->credit_nbits < 0) {
        c->credit_nbits += c->idle_slope_bps * (int64_t)dt;
        int64_t hi = c->hi_credit_bits * EP_NS_PER_SEC;
        if (queue_nonempty && c->credit_nbits > hi)
            c->credit_nbits = hi;
        if (!queue_nonempty && c->credit_nbits > 0)
            c->credit_nbits = 0; /* recovered from negative: stop at zero */
    } else if (c->credit_nbits > 0) {
        c->credit_nbits = 0; /* empty queue forfeits positive credit */
    }
}

bool ep_tsn_cbs_eligible(const ep_tsn_cbs_t *c)
{
    return c->credit_nbits >= 0;
}

void ep_tsn_cbs_on_tx(ep_tsn_cbs_t *c, uint64_t start_ns, size_t wire_bytes)
{
    ep_tsn_cbs_update(c, start_ns, true);
    uint64_t tx_ns = ((uint64_t)wire_bytes * 8u * 1000000000ull + c->port_bps - 1) / c->port_bps;
    c->credit_nbits += c->send_slope_bps * (int64_t)tx_ns;
    int64_t lo = c->lo_credit_bits * EP_NS_PER_SEC;
    if (c->credit_nbits < lo)
        c->credit_nbits = lo;
    c->last_ns = start_ns + tx_ns;
}

int64_t ep_tsn_cbs_credit_bits(const ep_tsn_cbs_t *c)
{
    return c->credit_nbits / EP_NS_PER_SEC;
}

uint64_t ep_tsn_cbs_time_to_eligible(const ep_tsn_cbs_t *c)
{
    if (c->credit_nbits >= 0)
        return 0;
    return (uint64_t)((-c->credit_nbits + c->idle_slope_bps - 1) / c->idle_slope_bps);
}

/* --------------------------- 802.1CB FRER -------------------------- */

void ep_tsn_frer_init(ep_tsn_frer_t *r, uint16_t history_len, uint64_t reset_timeout_ns)
{
    memset(r, 0, sizeof(*r));
    if (history_len < 2) history_len = 2;
    if (history_len > 64) history_len = 64;
    r->history_len = history_len;
    r->reset_timeout_ns = reset_timeout_ns;
    r->take_any = true;
}

bool ep_tsn_frer_accept(ep_tsn_frer_t *r, uint16_t seq, uint64_t now)
{
    if (r->reset_timeout_ns && !r->take_any && now - r->last_pass_ns > r->reset_timeout_ns) {
        r->take_any = true; /* RECOVERY_TIMEOUT: SequenceRecoveryReset */
        r->resets++;
    }
    if (r->take_any) {
        r->take_any = false;
        r->recov_seq = seq;
        r->history = 1;
        r->passed++;
        r->last_pass_ns = now;
        return true;
    }
    int32_t delta = (int16_t)(uint16_t)(seq - r->recov_seq); /* modulo 2^16, signed */
    if (delta >= r->history_len || delta <= -(int32_t)r->history_len) {
        r->rogue++;
        r->discarded++;
        return false;
    }
    if (delta <= 0) {
        uint64_t bit = 1ull << (unsigned)(-delta);
        if (r->history & bit) {
            r->discarded++; /* duplicate from the other path */
            return false;
        }
        r->history |= bit;
        r->out_of_order++;
    } else {
        if (delta > 1)
            r->lost += (uint32_t)(delta - 1); /* provisionally missing */
        r->history = (delta >= 64) ? 1 : ((r->history << delta) | 1);
        if (r->history_len < 64)
            r->history &= (1ull << r->history_len) - 1;
        r->recov_seq = seq;
    }
    r->passed++;
    r->last_pass_ns = now;
    return true;
}

/* ---------------------- Deterministic egress ----------------------- */

const uint8_t ep_tsn_default_pcp_to_tc[8] = {1, 0, 2, 3, 4, 5, 6, 7};

void ep_tsn_egress_init(ep_tsn_egress_t *eg, uint64_t link_bps, const ep_tsn_tas_t *tas)
{
    memset(eg, 0, sizeof(*eg));
    for (int i = 0; i < EP_TSN_POOL_SIZE; i++)
        eg->pool[i].next = (int16_t)(i + 1 < EP_TSN_POOL_SIZE ? i + 1 : -1);
    eg->free_head = 0;
    for (unsigned t = 0; t < EP_TSN_NUM_TC; t++)
        eg->q_head[t] = eg->q_tail[t] = -1;
    eg->q_limit = EP_TSN_POOL_SIZE / 2; /* no single class can exhaust the pool */
    memcpy(eg->pcp_to_tc, ep_tsn_default_pcp_to_tc, 8);
    eg->tas = tas;
    eg->link_bps = link_bps;
}

void ep_tsn_egress_set_cbs(ep_tsn_egress_t *eg, uint8_t tc, ep_tsn_cbs_t *cbs)
{
    if (tc < EP_TSN_NUM_TC)
        eg->cbs[tc] = cbs;
}

ep_tsn_frame_t *ep_tsn_egress_alloc(ep_tsn_egress_t *eg)
{
    int16_t i = eg->free_head;
    if (i < 0)
        return NULL;
    eg->free_head = eg->pool[i].next;
    ep_tsn_frame_t *f = &eg->pool[i];
    f->next = -1;
    f->len = 0;
    f->launch_ns = 0;
    return f;
}

void ep_tsn_egress_free(ep_tsn_egress_t *eg, ep_tsn_frame_t *f)
{
    int16_t i = (int16_t)(f - eg->pool);
    if (i < 0 || i >= EP_TSN_POOL_SIZE)
        return;
    f->next = eg->free_head;
    eg->free_head = i;
}

uint8_t ep_tsn_egress_classify(const ep_tsn_egress_t *eg, const uint8_t *frame, size_t len)
{
    ep_eth_hdr_t h;
    uint8_t pcp = 0;
    if (ep_eth_parse(frame, len, &h) == EP_OK && h.vlan)
        pcp = h.pcp;
    return eg->pcp_to_tc[pcp & 7];
}

ep_status_t ep_tsn_egress_enqueue(ep_tsn_egress_t *eg, ep_tsn_frame_t *f, uint8_t tc, uint64_t now)
{
    if (!f || tc >= EP_TSN_NUM_TC || f->len == 0 || f->len > EP_TSN_FRAME_MAX)
        return EP_ERR_PARAM;
    if (eg->q_len[tc] >= eg->q_limit) {
        eg->drops[tc]++;
        ep_tsn_egress_free(eg, f);
        return EP_ERR_OVERFLOW;
    }
    if (eg->cbs[tc])
        ep_tsn_cbs_update(eg->cbs[tc], now, eg->q_len[tc] > 0); /* settle credit first */
    int16_t i = (int16_t)(f - eg->pool);
    f->tc = tc;
    f->next = -1;
    if (eg->q_tail[tc] >= 0)
        eg->pool[eg->q_tail[tc]].next = i;
    else
        eg->q_head[tc] = i;
    eg->q_tail[tc] = i;
    eg->q_len[tc]++;
    eg->enqueued++;
    return EP_OK;
}

static uint64_t head_ready_at(ep_tsn_egress_t *eg, uint8_t tc, uint64_t now)
{
    const ep_tsn_frame_t *f = &eg->pool[eg->q_head[tc]];
    uint64_t t = f->launch_ns > now ? f->launch_ns : now;
    if (eg->cbs[tc]) {
        ep_tsn_cbs_update(eg->cbs[tc], now, true);
        uint64_t c = now + ep_tsn_cbs_time_to_eligible(eg->cbs[tc]);
        if (c > t) t = c;
    }
    if (eg->tas)
        t = ep_tsn_tas_next_tx(eg->tas, t, tc, f->len);
    return t;
}

ep_tsn_frame_t *ep_tsn_egress_dequeue(ep_tsn_egress_t *eg, uint64_t now)
{
    for (int tc = EP_TSN_NUM_TC - 1; tc >= 0; tc--) {
        if (eg->q_head[tc] < 0)
            continue;
        if (head_ready_at(eg, (uint8_t)tc, now) != now)
            continue; /* launch time, credit or gate not yet satisfied */
        int16_t i = eg->q_head[tc];
        ep_tsn_frame_t *f = &eg->pool[i];
        eg->q_head[tc] = f->next;
        if (eg->q_head[tc] < 0)
            eg->q_tail[tc] = -1;
        eg->q_len[tc]--;
        eg->dequeued++;
        if (eg->cbs[tc]) {
            size_t wire = (f->len < EP_ETH_MIN_LEN ? EP_ETH_MIN_LEN : f->len) +
                          EP_ETH_FCS_LEN + EP_ETH_PREAMBLE_SFD + EP_ETH_IFG;
            ep_tsn_cbs_on_tx(eg->cbs[tc], now, wire);
        }
        f->next = -1;
        return f;
    }
    return NULL;
}

uint64_t ep_tsn_egress_next_event(ep_tsn_egress_t *eg, uint64_t now)
{
    uint64_t best = EP_TSN_NEVER;
    for (unsigned tc = 0; tc < EP_TSN_NUM_TC; tc++) {
        if (eg->q_head[tc] < 0)
            continue;
        uint64_t t = head_ready_at(eg, (uint8_t)tc, now);
        if (t < best)
            best = t;
    }
    return best;
}
