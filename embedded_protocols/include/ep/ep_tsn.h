/*
 * ep_tsn.h - Time-Sensitive Networking (IEEE 802.1) egress building blocks.
 *
 *   802.1Qbv  Time-Aware Shaper (TAS): cyclic gate control list (GCL)
 *             aligned to PTP time; guard band so a frame never overruns a
 *             gate close; next transmission opportunity; Linux taprio
 *             command generation.
 *   802.1Qav  Credit-Based Shaper (CBS) for AVB stream reservation classes.
 *   802.1CB   Frame Replication and Elimination for Reliability (FRER):
 *             vector recovery algorithm (duplicate elimination).
 *   802.1Q    PCP -> traffic-class mapping and a deterministic egress
 *             scheduler: static frame pool, per-class FIFOs, strict
 *             priority + TAS + CBS + LaunchTime, O(classes) per decision.
 *
 * All times are PTP (TAI) nanoseconds from the gPTP-disciplined clock.
 */
#ifndef EP_TSN_H
#define EP_TSN_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_TSN_NUM_TC 8u
#define EP_TSN_NEVER  UINT64_MAX

/* --------------------------- 802.1Qbv TAS -------------------------- */
typedef enum {
    EP_GCL_SET_GATES = 0,
    EP_GCL_SET_AND_HOLD = 1,    /* 802.1Qbu: hold preemptible MAC  */
    EP_GCL_SET_AND_RELEASE = 2  /* 802.1Qbu: release preemptible   */
} ep_gcl_op_t;

typedef struct {
    uint8_t op;        /* ep_gcl_op_t                         */
    uint8_t gate_mask; /* bit n = traffic class n gate open    */
    uint32_t interval_ns;
} ep_tsn_gcl_entry_t;

typedef struct {
    const ep_tsn_gcl_entry_t *gcl;
    size_t n;
    uint64_t base_time_ns;
    uint64_t cycle_time_ns; /* < sum: list truncated, > sum: last entry extended */
    uint64_t link_bps;
} ep_tsn_tas_t;

ep_status_t ep_tsn_tas_init(ep_tsn_tas_t *tas, const ep_tsn_gcl_entry_t *gcl, size_t n,
                            uint64_t base_time_ns, uint64_t cycle_time_ns, uint64_t link_bps);
/* First cycle start >= now (802.1Qbv: base time in the past is advanced
 * by an integer number of cycles). */
uint64_t ep_tsn_tas_next_cycle_start(const ep_tsn_tas_t *tas, uint64_t now);
/* Gate states at `now`; *entry_end = time of the next GCL transition. */
uint8_t ep_tsn_tas_gates(const ep_tsn_tas_t *tas, uint64_t now, uint64_t *entry_end, size_t *idx);
/* Is tc's gate open at now? *close_at = when it closes (EP_TSN_NEVER if never). */
bool ep_tsn_tas_gate_open(const ep_tsn_tas_t *tas, uint64_t now, uint8_t tc, uint64_t *close_at);
/* Gate open now AND the whole frame (incl. preamble/IFG) fits before close. */
bool ep_tsn_tas_can_tx(const ep_tsn_tas_t *tas, uint64_t now, uint8_t tc, size_t frame_len);
/* Earliest t >= now at which ep_tsn_tas_can_tx() holds, EP_TSN_NEVER if none. */
uint64_t ep_tsn_tas_next_tx(const ep_tsn_tas_t *tas, uint64_t now, uint8_t tc, size_t frame_len);

/* "tc qdisc replace dev <if> parent root handle 100 taprio ..." for Linux.
 * Returns snprintf-style length. */
int ep_tsn_taprio_cmd(const ep_tsn_tas_t *tas, const char *ifname, uint8_t num_tc,
                      const uint8_t prio_tc_map[16], bool hw_offload, char *buf, size_t cap);

/* --------------------------- 802.1Qav CBS -------------------------- */
typedef struct {
    int64_t idle_slope_bps;
    int64_t send_slope_bps; /* idle - port rate (negative) */
    int64_t hi_credit_bits;
    int64_t lo_credit_bits;
    int64_t credit_nbits;   /* credit in bit * 1e-9 units (exact integer maths) */
    uint64_t port_bps;
    uint64_t last_ns;
} ep_tsn_cbs_t;

ep_status_t ep_tsn_cbs_init(ep_tsn_cbs_t *c, uint64_t idle_slope_bps, uint64_t port_bps,
                            uint32_t max_frame_bytes, uint32_t max_interference_bytes);
void ep_tsn_cbs_update(ep_tsn_cbs_t *c, uint64_t now, bool queue_nonempty);
bool ep_tsn_cbs_eligible(const ep_tsn_cbs_t *c);
void ep_tsn_cbs_on_tx(ep_tsn_cbs_t *c, uint64_t start_ns, size_t wire_bytes);
int64_t ep_tsn_cbs_credit_bits(const ep_tsn_cbs_t *c);
uint64_t ep_tsn_cbs_time_to_eligible(const ep_tsn_cbs_t *c);

/* --------------------------- 802.1CB FRER -------------------------- */
typedef struct {
    uint16_t history_len; /* 2..64 */
    uint64_t history;     /* bit k = RecovSeqNum - k already seen */
    uint16_t recov_seq;
    bool take_any;
    uint64_t reset_timeout_ns;
    uint64_t last_pass_ns;
    uint32_t passed, discarded, out_of_order, rogue, lost, resets;
} ep_tsn_frer_t;

void ep_tsn_frer_init(ep_tsn_frer_t *r, uint16_t history_len, uint64_t reset_timeout_ns);
/* true = pass the frame up, false = eliminate (duplicate or rogue). */
bool ep_tsn_frer_accept(ep_tsn_frer_t *r, uint16_t seq, uint64_t now);

/* ---------------------- Deterministic egress ----------------------- */
#ifndef EP_TSN_POOL_SIZE
#define EP_TSN_POOL_SIZE 32
#endif
#ifndef EP_TSN_FRAME_MAX
#define EP_TSN_FRAME_MAX 1522
#endif

typedef struct {
    uint16_t len;
    uint8_t tc;
    int16_t next;
    uint64_t launch_ns; /* 0 = as soon as possible (802.1Qbv/ETF LaunchTime) */
    uint8_t data[EP_TSN_FRAME_MAX];
} ep_tsn_frame_t;

typedef struct {
    ep_tsn_frame_t pool[EP_TSN_POOL_SIZE];
    int16_t free_head;
    int16_t q_head[EP_TSN_NUM_TC], q_tail[EP_TSN_NUM_TC];
    uint16_t q_len[EP_TSN_NUM_TC];
    uint16_t q_limit;
    uint8_t pcp_to_tc[8];
    const ep_tsn_tas_t *tas;
    ep_tsn_cbs_t *cbs[EP_TSN_NUM_TC];
    uint64_t link_bps;
    uint32_t enqueued, dequeued, drops[EP_TSN_NUM_TC];
} ep_tsn_egress_t;

extern const uint8_t ep_tsn_default_pcp_to_tc[8]; /* IEEE 802.1Q Table 8-5, 8 classes */

void ep_tsn_egress_init(ep_tsn_egress_t *eg, uint64_t link_bps, const ep_tsn_tas_t *tas);
void ep_tsn_egress_set_cbs(ep_tsn_egress_t *eg, uint8_t tc, ep_tsn_cbs_t *cbs);
ep_tsn_frame_t *ep_tsn_egress_alloc(ep_tsn_egress_t *eg);
void ep_tsn_egress_free(ep_tsn_egress_t *eg, ep_tsn_frame_t *f);
/* Traffic class from the frame's 802.1Q PCP (untagged -> PCP 0). */
uint8_t ep_tsn_egress_classify(const ep_tsn_egress_t *eg, const uint8_t *frame, size_t len);
ep_status_t ep_tsn_egress_enqueue(ep_tsn_egress_t *eg, ep_tsn_frame_t *f, uint8_t tc, uint64_t now);
/* Highest-priority frame allowed on the wire at `now`, or NULL. The caller
 * transmits it and then calls ep_tsn_egress_free(). */
ep_tsn_frame_t *ep_tsn_egress_dequeue(ep_tsn_egress_t *eg, uint64_t now);
/* Earliest time a queued frame may become eligible (program a timer). */
uint64_t ep_tsn_egress_next_event(ep_tsn_egress_t *eg, uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* EP_TSN_H */
