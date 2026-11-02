#include "ep/ep_eth.h"
#include "ep_test.h"

static const uint8_t MAC_A[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
static const uint8_t MAC_B[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0B};

static void test_vlan_build_parse(void)
{
    uint8_t buf[128];
    ep_eth_hdr_t h = {.ethertype = 0x22F0, .vlan = true, .pcp = 3, .dei = false, .vid = 2};
    memcpy(h.dst, MAC_B, 6);
    memcpy(h.src, MAC_A, 6);
    const uint8_t payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    size_t n = ep_eth_build(buf, sizeof(buf), &h, payload, sizeof(payload));
    CHECK_EQ(n, 60); /* padded to minimum */
    CHECK_EQ(buf[12], 0x81);
    CHECK_EQ(buf[13], 0x00);
    CHECK_EQ(buf[14], 0x60); /* PCP 3 << 5 */
    CHECK_EQ(buf[15], 0x02);

    ep_eth_hdr_t p;
    CHECK_EQ(ep_eth_parse(buf, n, &p), EP_OK);
    CHECK(p.vlan);
    CHECK_EQ(p.pcp, 3);
    CHECK_EQ(p.vid, 2);
    CHECK_EQ(p.ethertype, 0x22F0);
    CHECK_MEM(p.payload, payload, 10);
    CHECK_MEM(p.src, MAC_A, 6);

    h.vid = 4096;
    CHECK_EQ(ep_eth_build(buf, sizeof(buf), &h, payload, 10), 0);
}

static void test_fcs(void)
{
    uint8_t f[70];
    ep_eth_hdr_t h = {.ethertype = 0x88B5};
    memcpy(h.dst, MAC_B, 6);
    memcpy(h.src, MAC_A, 6);
    size_t n = ep_eth_build(f, sizeof(f), &h, (const uint8_t *)"hello", 5);
    n = ep_eth_append_fcs(f, n, sizeof(f));
    CHECK_EQ(n, 64);
    CHECK(ep_eth_check_fcs(f, n));
    f[20] ^= 0x10;
    CHECK(!ep_eth_check_fcs(f, n));
}

static void test_wire_time(void)
{
    /* minimum frame at 1 Gbit/s: 84 byte times = 672 ns */
    CHECK_EQ(ep_eth_wire_time_ns(60, 1000000000ull), 672);
    CHECK_EQ(ep_eth_wire_time_ns(1514, 1000000000ull), 12304);
    CHECK_EQ(ep_eth_wire_time_ns(1514, 100000000ull), 123040);
}

static void test_inet_checksum(void)
{
    /* RFC 1071 style example (Wikipedia "IPv4 header checksum") */
    const uint8_t ip[20] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                            0x00, 0x00, 0xC0, 0xA8, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0xC7};
    CHECK_EQ(ep_inet_checksum(ip, 20, 0), 0xB861);
}

static void test_udp_roundtrip(void)
{
    uint8_t buf[128];
    const uint8_t msg[44] = {0x00, 0x02};
    ep_udp_dgram_t d = {.src_ip = 0xC0A80164, .dst_ip = 0xE0000181, /* 224.0.1.129 PTP */
                        .src_port = 319, .dst_port = 319, .ttl = 1, .dscp = 46,
                        .payload = msg, .payload_len = sizeof(msg)};
    size_t n = ep_ipv4_udp_build(buf, sizeof(buf), &d);
    CHECK_EQ(n, 20 + 8 + 44);
    ep_udp_dgram_t p;
    CHECK_EQ(ep_ipv4_udp_parse(buf, n, &p), EP_OK);
    CHECK_EQ(p.dst_ip, 0xE0000181);
    CHECK_EQ(p.dst_port, 319);
    CHECK_EQ(p.dscp, 46);
    CHECK_EQ(p.payload_len, 44);
    buf[30] ^= 1; /* payload corruption -> UDP checksum */
    CHECK_EQ(ep_ipv4_udp_parse(buf, n, &p), EP_ERR_CRC);
    buf[30] ^= 1;
    buf[8] = 0; /* TTL changed without fixing header checksum */
    CHECK_EQ(ep_ipv4_udp_parse(buf, n, &p), EP_ERR_CRC);
}

static void test_arp(void)
{
    uint8_t b[28];
    ep_arp_t req = {.op = 1, .spa = 0x0A000001, .tpa = 0x0A000002};
    memcpy(req.sha, MAC_A, 6);
    CHECK_EQ(ep_arp_build(b, sizeof(b), &req), 28);
    ep_arp_t p;
    CHECK_EQ(ep_arp_parse(b, 28, &p), EP_OK);
    CHECK_EQ(p.op, 1);
    CHECK_EQ(p.tpa, 0x0A000002);
    CHECK_MEM(p.sha, MAC_A, 6);
}

/* ---------------- PHY over a register-file MDIO mock ---------------- */
typedef struct {
    uint16_t r[32];
    int reset_reads;
} phy_t;

static ep_status_t mdio_rd(void *ctx, uint8_t phy, uint8_t reg, uint16_t *v)
{
    phy_t *p = ctx;
    if (phy != 1)
        { *v = 0xFFFF; return EP_OK; }
    if (reg == EP_MII_BMCR && (p->r[0] & EP_BMCR_RESET) && ++p->reset_reads >= 3)
        p->r[0] &= (uint16_t)~EP_BMCR_RESET; /* reset completes after a while */
    *v = p->r[reg];
    return EP_OK;
}
static ep_status_t mdio_wr(void *ctx, uint8_t phy, uint8_t reg, uint16_t v)
{
    phy_t *p = ctx;
    if (phy == 1)
        p->r[reg] = v;
    return EP_OK;
}

static void test_phy(void)
{
    phy_t p = {0};
    p.r[EP_MII_PHYID1] = 0x0007;
    p.r[EP_MII_PHYID2] = 0xC0F1; /* LAN8720-like */
    ep_mdio_t m = {mdio_rd, mdio_wr, &p};
    uint32_t id;
    CHECK_EQ(ep_phy_read_id(&m, 1, &id), EP_OK);
    CHECK_EQ(id, 0x0007C0F1);
    CHECK_EQ(ep_phy_read_id(&m, 2, &id), EP_ERR_NACK);
    CHECK_EQ(ep_phy_reset(&m, 1, NULL, 10), EP_OK);

    ep_link_t l;
    p.r[EP_MII_BMCR] = EP_BMCR_ANENABLE;
    p.r[EP_MII_BMSR] = 0;
    CHECK_EQ(ep_phy_get_link(&m, 1, &l), EP_OK);
    CHECK(!l.up);
    p.r[EP_MII_BMSR] = EP_BMSR_LSTATUS | EP_BMSR_ANEGCOMPLETE;
    p.r[EP_MII_ANAR] = 0x01E1;   /* 100FD 100HD 10FD 10HD */
    p.r[EP_MII_ANLPAR] = 0x00A1; /* partner: 100HD + 10HD */
    CHECK_EQ(ep_phy_get_link(&m, 1, &l), EP_OK);
    CHECK(l.up);
    CHECK_EQ(l.speed_mbps, 100);
    CHECK(!l.full_duplex);
    p.r[EP_MII_CTRL1000] = 0x0200;
    p.r[EP_MII_STAT1000] = 0x0800;
    CHECK_EQ(ep_phy_get_link(&m, 1, &l), EP_OK);
    CHECK_EQ(l.speed_mbps, 1000);
    CHECK(l.full_duplex);
}

/* ---------------- EtherType dispatch ---------------- */
static int ptp_hits, arp_hits;
static void on_ptp(void *arg, const ep_eth_hdr_t *h, uint64_t ts)
{
    (void)arg;
    (void)h;
    if (ts == 123)
        ptp_hits++;
}
static void on_arp(void *arg, const ep_eth_hdr_t *h, uint64_t ts)
{
    (void)arg;
    (void)ts;
    if (h->ethertype == EP_ETHERTYPE_ARP)
        arp_hits++;
}

static void test_stack_dispatch(void)
{
    ep_eth_stack_t s;
    ep_eth_stack_init(&s, MAC_A);
    ep_eth_stack_register(&s, EP_ETHERTYPE_PTP, on_ptp, NULL);
    ep_eth_stack_register(&s, EP_ETHERTYPE_ARP, on_arp, NULL);
    uint8_t f[64];
    ep_eth_hdr_t h = {.ethertype = EP_ETHERTYPE_PTP};
    static const uint8_t ptp_mc[6] = {0x01, 0x1B, 0x19, 0x00, 0x00, 0x00};
    memcpy(h.dst, ptp_mc, 6);
    memcpy(h.src, MAC_B, 6);
    size_t n = ep_eth_build(f, sizeof(f), &h, NULL, 0);
    ep_eth_stack_input(&s, f, n, 123);
    CHECK_EQ(ptp_hits, 1);
    memcpy(h.dst, MAC_B, 6); /* unicast to someone else */
    n = ep_eth_build(f, sizeof(f), &h, NULL, 0);
    ep_eth_stack_input(&s, f, n, 123);
    CHECK_EQ(s.rx_dropped_dst, 1);
    memcpy(h.dst, MAC_A, 6);
    h.ethertype = EP_ETHERTYPE_ARP;
    n = ep_eth_build(f, sizeof(f), &h, NULL, 0);
    ep_eth_stack_input(&s, f, n, 0);
    CHECK_EQ(arp_hits, 1);
    h.ethertype = 0x1234;
    n = ep_eth_build(f, sizeof(f), &h, NULL, 0);
    ep_eth_stack_input(&s, f, n, 0);
    CHECK_EQ(s.rx_dropped_type, 1);
}

int main(void)
{
    RUN(test_vlan_build_parse);
    RUN(test_fcs);
    RUN(test_wire_time);
    RUN(test_inet_checksum);
    RUN(test_udp_roundtrip);
    RUN(test_arp);
    RUN(test_phy);
    RUN(test_stack_dispatch);
    TEST_MAIN_END();
}
