/*
 * ep_eth.h - Lightweight deterministic Ethernet stack pieces.
 *
 *   - L2 frame build/parse incl. IEEE 802.1Q VLAN tag (PCP/DEI/VID)
 *   - FCS (CRC-32) for MACs without hardware FCS
 *   - IPv4 + UDP (PTP over UDP uses ports 319/320), ARP request/reply
 *   - IEEE 802.3 Clause 22 PHY management over MDIO
 *   - Static EtherType dispatch table (no heap, bounded work per frame)
 */
#ifndef EP_ETH_H
#define EP_ETH_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_ETH_ALEN       6u
#define EP_ETH_HLEN       14u
#define EP_ETH_VLAN_HLEN  4u
#define EP_ETH_MIN_LEN    60u   /* without FCS */
#define EP_ETH_MAX_LEN    1514u /* without FCS, untagged */
#define EP_ETH_FCS_LEN    4u
#define EP_ETH_PREAMBLE_SFD 8u
#define EP_ETH_IFG        12u

#define EP_ETHERTYPE_IPV4 0x0800u
#define EP_ETHERTYPE_ARP  0x0806u
#define EP_ETHERTYPE_VLAN 0x8100u
#define EP_ETHERTYPE_QINQ 0x88A8u
#define EP_ETHERTYPE_PTP  0x88F7u /* IEEE 1588 / 802.1AS over L2     */
#define EP_ETHERTYPE_AVTP 0x22F0u /* IEEE 1722 audio/video transport */
#define EP_ETHERTYPE_LLDP 0x88CCu
#define EP_ETHERTYPE_RTAG 0xF1C1u /* IEEE 802.1CB redundancy tag     */

typedef struct {
    uint8_t dst[EP_ETH_ALEN];
    uint8_t src[EP_ETH_ALEN];
    uint16_t ethertype;
    bool vlan;
    uint8_t pcp; /* 0..7 priority code point  */
    bool dei;
    uint16_t vid; /* 0..4095                 */
    const uint8_t *payload;
    size_t payload_len;
} ep_eth_hdr_t;

/* Builds header+payload, pads to 60 bytes. Returns length or 0. */
size_t ep_eth_build(uint8_t *buf, size_t cap, const ep_eth_hdr_t *h,
                    const uint8_t *payload, size_t len);
ep_status_t ep_eth_parse(const uint8_t *frame, size_t len, ep_eth_hdr_t *out);
/* Appends FCS (little-endian on the wire). Returns new length or 0. */
size_t ep_eth_append_fcs(uint8_t *frame, size_t len, size_t cap);
bool ep_eth_check_fcs(const uint8_t *frame, size_t len_with_fcs);
/* Wire time including preamble, SFD, FCS and IFG. */
uint64_t ep_eth_wire_time_ns(size_t frame_len_no_fcs, uint64_t link_bps);
bool ep_eth_is_multicast(const uint8_t mac[EP_ETH_ALEN]);

/* --------------------------- IPv4 / UDP ---------------------------- */
#define EP_IPV4_HLEN 20u
#define EP_UDP_HLEN  8u
#define EP_IPPROTO_UDP 17u

typedef struct {
    uint32_t src_ip, dst_ip; /* host byte order */
    uint16_t src_port, dst_port;
    uint8_t ttl, dscp;
    uint16_t ip_id;
    const uint8_t *payload;
    size_t payload_len;
} ep_udp_dgram_t;

uint16_t ep_inet_checksum(const uint8_t *data, size_t len, uint32_t initial);
/* Writes IPv4+UDP header and payload. Returns IP datagram length or 0. */
size_t ep_ipv4_udp_build(uint8_t *buf, size_t cap, const ep_udp_dgram_t *d);
ep_status_t ep_ipv4_udp_parse(const uint8_t *ip, size_t len, ep_udp_dgram_t *out);

/* ------------------------------ ARP -------------------------------- */
#define EP_ARP_LEN 28u
typedef struct {
    uint16_t op; /* 1 request, 2 reply */
    uint8_t sha[EP_ETH_ALEN];
    uint32_t spa;
    uint8_t tha[EP_ETH_ALEN];
    uint32_t tpa;
} ep_arp_t;

size_t ep_arp_build(uint8_t *buf, size_t cap, const ep_arp_t *a);
ep_status_t ep_arp_parse(const uint8_t *buf, size_t len, ep_arp_t *out);

/* -------------------------- PHY (Clause 22) ------------------------ */
#define EP_MII_BMCR   0x00
#define EP_MII_BMSR   0x01
#define EP_MII_PHYID1 0x02
#define EP_MII_PHYID2 0x03
#define EP_MII_ANAR   0x04
#define EP_MII_ANLPAR 0x05
#define EP_MII_CTRL1000 0x09
#define EP_MII_STAT1000 0x0A

#define EP_BMCR_RESET    0x8000u
#define EP_BMCR_LOOPBACK 0x4000u
#define EP_BMCR_SPEED100 0x2000u
#define EP_BMCR_ANENABLE 0x1000u
#define EP_BMCR_ANRESTART 0x0200u
#define EP_BMCR_FULLDPLX 0x0100u
#define EP_BMCR_SPEED1000 0x0040u
#define EP_BMSR_ANEGCOMPLETE 0x0020u
#define EP_BMSR_LSTATUS  0x0004u

typedef struct {
    ep_status_t (*read)(void *ctx, uint8_t phy, uint8_t reg, uint16_t *val);
    ep_status_t (*write)(void *ctx, uint8_t phy, uint8_t reg, uint16_t val);
    void *ctx;
} ep_mdio_t;

typedef struct {
    bool up;
    uint32_t speed_mbps; /* 10/100/1000 */
    bool full_duplex;
} ep_link_t;

ep_status_t ep_phy_reset(const ep_mdio_t *m, uint8_t phy, const ep_clock_t *clk, uint32_t timeout_ms);
ep_status_t ep_phy_read_id(const ep_mdio_t *m, uint8_t phy, uint32_t *oui_model_rev);
ep_status_t ep_phy_autoneg_restart(const ep_mdio_t *m, uint8_t phy);
ep_status_t ep_phy_force(const ep_mdio_t *m, uint8_t phy, uint32_t speed_mbps, bool full_duplex);
ep_status_t ep_phy_get_link(const ep_mdio_t *m, uint8_t phy, ep_link_t *link);

/* ------------------------ EtherType dispatch ----------------------- */
typedef void (*ep_eth_handler_t)(void *arg, const ep_eth_hdr_t *hdr, uint64_t rx_ts_ns);

#define EP_ETH_MAX_HANDLERS 8u

typedef struct {
    uint8_t mac[EP_ETH_ALEN];
    struct {
        uint16_t ethertype;
        ep_eth_handler_t fn;
        void *arg;
    } handlers[EP_ETH_MAX_HANDLERS];
    size_t n_handlers;
    uint32_t rx_frames, rx_dropped_dst, rx_dropped_type, rx_errors;
} ep_eth_stack_t;

void ep_eth_stack_init(ep_eth_stack_t *s, const uint8_t mac[EP_ETH_ALEN]);
ep_status_t ep_eth_stack_register(ep_eth_stack_t *s, uint16_t ethertype, ep_eth_handler_t fn, void *arg);
void ep_eth_stack_input(ep_eth_stack_t *s, const uint8_t *frame, size_t len, uint64_t rx_ts_ns);

#ifdef __cplusplus
}
#endif

#endif /* EP_ETH_H */
