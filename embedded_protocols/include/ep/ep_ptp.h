/*
 * ep_ptp.h - IEEE 1588-2019 (PTPv2.1) / IEEE 802.1AS (gPTP) building blocks.
 *
 *   - Wire codec for the common header and Sync, Delay_Req, Follow_Up,
 *     Delay_Resp, Pdelay_Req/Resp/Resp_Follow_Up and Announce messages.
 *   - End-to-end (E2E) and peer-to-peer (P2P) delay/offset computation,
 *     neighbour rate ratio.
 *   - Best Master Clock Algorithm dataset comparison (1588 clause 9.3.4).
 *   - PI clock servo (unlocked -> jump -> locked), output in ppb.
 *   - Ordinary clock, slave-only, E2E delay mechanism, one- or two-step
 *     masters, with foreign-master qualification and announce timeout.
 *
 * Timestamps are int64 nanoseconds of PTP time (TAI). Hardware
 * timestamping (MAC/PHY) is strongly recommended; the stack only needs
 * the ingress timestamp of event messages and the egress timestamp of
 * the Delay_Req it sends.
 */
#ifndef EP_PTP_H
#define EP_PTP_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_PTP_EVENT_PORT   319u
#define EP_PTP_GENERAL_PORT 320u
#define EP_PTP_HDR_LEN      34u

/* L2 multicast addresses */
extern const uint8_t EP_PTP_MAC_PRIMARY[6]; /* 01-1B-19-00-00-00 (all except peer delay) */
extern const uint8_t EP_PTP_MAC_PDELAY[6];  /* 01-80-C2-00-00-0E (peer delay, gPTP)      */

typedef enum {
    EP_PTP_SYNC = 0x0,
    EP_PTP_DELAY_REQ = 0x1,
    EP_PTP_PDELAY_REQ = 0x2,
    EP_PTP_PDELAY_RESP = 0x3,
    EP_PTP_FOLLOW_UP = 0x8,
    EP_PTP_DELAY_RESP = 0x9,
    EP_PTP_PDELAY_RESP_FUP = 0xA,
    EP_PTP_ANNOUNCE = 0xB,
    EP_PTP_SIGNALING = 0xC,
    EP_PTP_MANAGEMENT = 0xD
} ep_ptp_msg_type_t;

/* flagField bits (octet 0 << 8 | octet 1) */
#define EP_PTP_FLAG_ALT_MASTER   0x0100u
#define EP_PTP_FLAG_TWO_STEP     0x0200u
#define EP_PTP_FLAG_UNICAST      0x0400u
#define EP_PTP_FLAG_LI61         0x0001u
#define EP_PTP_FLAG_LI59         0x0002u
#define EP_PTP_FLAG_UTC_VALID    0x0004u
#define EP_PTP_FLAG_PTP_TIMESCALE 0x0008u
#define EP_PTP_FLAG_TIME_TRACEABLE 0x0010u
#define EP_PTP_FLAG_FREQ_TRACEABLE 0x0020u

typedef struct {
    uint8_t clock_id[8];
    uint16_t port;
} ep_ptp_port_id_t;

typedef struct {
    uint8_t msg_type;      /* ep_ptp_msg_type_t   */
    uint8_t sdo_id_major;  /* transportSpecific: 1 for gPTP */
    uint8_t version;       /* 2                   */
    uint8_t minor_version; /* 1 for 1588-2019     */
    uint16_t length;
    uint8_t domain;
    uint8_t sdo_id_minor;
    uint16_t flags;
    int64_t correction;    /* scaled ns (ns << 16) */
    uint32_t msg_specific;
    ep_ptp_port_id_t source;
    uint16_t sequence_id;
    uint8_t control;       /* deprecated, kept for v1 compatibility */
    int8_t log_interval;
} ep_ptp_hdr_t;

typedef struct {
    uint8_t clock_class;
    uint8_t accuracy;
    uint16_t variance; /* offsetScaledLogVariance */
} ep_ptp_clock_quality_t;

typedef struct {
    int64_t origin_ts;
    int16_t utc_offset;
    uint8_t gm_priority1;
    ep_ptp_clock_quality_t gm_quality;
    uint8_t gm_priority2;
    uint8_t gm_identity[8];
    uint16_t steps_removed;
    uint8_t time_source;
} ep_ptp_announce_t;

/* Timestamp <-> wire (48-bit seconds + 32-bit ns). */
void ep_ptp_ts_pack(uint8_t out[10], int64_t ns);
int64_t ep_ptp_ts_unpack(const uint8_t in[10]);
static inline int64_t ep_ptp_corr_to_ns(int64_t corr) { return corr >> 16; }
static inline int64_t ep_ptp_ns_to_corr(int64_t ns) { return ns * 65536; }

void ep_ptp_hdr_pack(uint8_t *buf, const ep_ptp_hdr_t *h);
ep_status_t ep_ptp_hdr_unpack(const uint8_t *buf, size_t len, ep_ptp_hdr_t *h);
uint16_t ep_ptp_msg_len(ep_ptp_msg_type_t t);
bool ep_ptp_is_event(ep_ptp_msg_type_t t);

/* Build messages; hdr->length/msg_type are filled in. Return length or 0. */
size_t ep_ptp_build_ts_msg(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, ep_ptp_msg_type_t type,
                           int64_t ts_ns);   /* Sync, Delay_Req, Follow_Up, Pdelay_Req */
size_t ep_ptp_build_resp(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, ep_ptp_msg_type_t type,
                         int64_t ts_ns, const ep_ptp_port_id_t *requester); /* Delay_Resp, Pdelay_Resp(_FUP) */
size_t ep_ptp_build_announce(uint8_t *buf, size_t cap, ep_ptp_hdr_t *hdr, const ep_ptp_announce_t *a);

int64_t ep_ptp_body_ts(const uint8_t *msg);                       /* first 10 body bytes */
void ep_ptp_body_port_id(const uint8_t *msg, ep_ptp_port_id_t *p); /* bytes 44..53       */
ep_status_t ep_ptp_parse_announce(const uint8_t *msg, size_t len, ep_ptp_announce_t *a);

bool ep_ptp_port_id_equal(const ep_ptp_port_id_t *a, const ep_ptp_port_id_t *b);
/* EUI-64 clock identity from a 48-bit MAC (inserts FF:FE). */
void ep_ptp_clock_id_from_mac(uint8_t id[8], const uint8_t mac[6]);

/* --------------------------- Delay maths --------------------------- */

/* E2E:  t1 Sync egress (master), t2 Sync ingress (slave),
 *       t3 Delay_Req egress (slave), t4 Delay_Req ingress (master).
 * corr_ms: correctionField of Sync (+Follow_Up), corr_sm: of Delay_Resp (ns). */
void ep_ptp_e2e(int64_t t1, int64_t t2, int64_t t3, int64_t t4,
                int64_t corr_ms, int64_t corr_sm, int64_t *offset, int64_t *mean_path_delay);

/* P2P: t1 Pdelay_Req egress, t2 ingress at peer, t3 Pdelay_Resp egress at
 * peer, t4 ingress here. rate_ratio = neighbour/local frequency ratio. */
int64_t ep_ptp_p2p_link_delay(int64_t t1, int64_t t2, int64_t t3, int64_t t4,
                              int64_t corr_ns, double rate_ratio);

/* Neighbour rate ratio from two (t3 peer, t4 local) pairs. */
double ep_ptp_rate_ratio(int64_t t3_old, int64_t t4_old, int64_t t3_new, int64_t t4_new);

/* ------------------------------- BMCA ------------------------------ */
typedef struct {
    uint8_t priority1;
    ep_ptp_clock_quality_t quality;
    uint8_t priority2;
    uint8_t gm_identity[8];
    uint16_t steps_removed;
    ep_ptp_port_id_t sender;   /* sourcePortIdentity of the Announce */
    ep_ptp_port_id_t receiver; /* our port that received it          */
} ep_ptp_dataset_t;

/* < 0: a is better, > 0: b is better, 0: identical. */
int ep_ptp_bmca_compare(const ep_ptp_dataset_t *a, const ep_ptp_dataset_t *b);

/* ------------------------------- Servo ----------------------------- */
typedef enum { EP_SERVO_UNLOCKED = 0, EP_SERVO_JUMP, EP_SERVO_LOCKED } ep_servo_state_t;

typedef struct {
    double kp, ki;
    double max_ppb;
    int64_t step_threshold_ns;       /* step when |offset| exceeds (0 = never)  */
    int64_t first_step_threshold_ns; /* step on first lock if exceeded          */
    double drift_ppb;                /* integrator (local clock frequency error) */
    int count;
    int64_t offset[2];
    uint64_t local[2];
    bool first_update_done;
} ep_ptp_servo_t;

void ep_ptp_servo_init(ep_ptp_servo_t *s, double kp, double ki, double max_ppb);
void ep_ptp_servo_reset(ep_ptp_servo_t *s);
/* offset = local - master (ns). Returns frequency adjustment in ppb to
 * APPLY to the local clock (positive = speed up). On EP_SERVO_JUMP the
 * caller must step the clock by -offset first. */
double ep_ptp_servo_sample(ep_ptp_servo_t *s, int64_t offset_ns, uint64_t local_ts_ns,
                           ep_servo_state_t *state);

/* ------------------------ Ordinary clock (slave) ------------------- */
typedef struct {
    ep_status_t (*adj_freq)(void *ctx, double ppb); /* absolute frequency offset */
    ep_status_t (*step)(void *ctx, int64_t delta_ns);
    int64_t (*now)(void *ctx);                      /* PTP time of the local clock */
    void *ctx;
} ep_ptp_clock_ops_t;

typedef enum {
    EP_PTP_PORT_LISTENING = 0,
    EP_PTP_PORT_UNCALIBRATED,
    EP_PTP_PORT_SLAVE
} ep_ptp_port_state_t;

#define EP_PTP_FOREIGN_MAX 4
#define EP_PTP_DELAY_FILTER 8

typedef struct {
    ep_ptp_port_id_t self;
    uint8_t domain;
    uint8_t sdo_id_major;
    int8_t log_announce_interval;
    uint8_t announce_receipt_timeout; /* intervals, default 3 */
    ep_ptp_clock_ops_t clock;
    /* Transmit; for event messages the port must return the egress time. */
    ep_status_t (*send)(void *arg, const uint8_t *msg, size_t len, bool event, int64_t *tx_ts);
    void *send_arg;
} ep_ptp_oc_config_t;

typedef struct {
    ep_ptp_dataset_t ds;
    uint8_t announces;
    int64_t last_rx;
} ep_ptp_foreign_t;

typedef struct {
    ep_ptp_oc_config_t cfg;
    ep_ptp_servo_t servo;
    ep_ptp_port_state_t state;

    ep_ptp_foreign_t foreign[EP_PTP_FOREIGN_MAX];
    int master_idx; /* -1 = none */
    int64_t last_announce_rx;

    /* Sync / Follow_Up matching */
    bool sync_pending;
    uint16_t sync_seq;
    int64_t t1, t2, corr_sync;   /* last complete Sync      */
    int64_t pend_t2, pend_corr;  /* two-step Sync in flight */

    /* Delay_Req / Delay_Resp */
    bool dreq_pending;
    uint16_t dreq_seq;
    int64_t t3;
    int64_t delay_samples[EP_PTP_DELAY_FILTER];
    unsigned n_delay;
    unsigned delay_idx;
    int64_t mean_path_delay;
    bool have_delay;

    int64_t last_offset;
    double last_ppb;
    ep_servo_state_t servo_state;
    uint32_t syncs, delay_resps, steps, seq_errors;
} ep_ptp_oc_t;

void ep_ptp_oc_config_default(ep_ptp_oc_config_t *cfg);
ep_status_t ep_ptp_oc_init(ep_ptp_oc_t *oc, const ep_ptp_oc_config_t *cfg);
/* rx_ts: ingress timestamp (local clock) for event messages. */
void ep_ptp_oc_rx(ep_ptp_oc_t *oc, const uint8_t *msg, size_t len, int64_t rx_ts);
/* Periodic housekeeping (announce timeout). now = local PTP time. */
void ep_ptp_oc_poll(ep_ptp_oc_t *oc, int64_t now);

#ifdef __cplusplus
}
#endif

#endif /* EP_PTP_H */
