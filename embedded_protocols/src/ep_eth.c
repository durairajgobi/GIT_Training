#include "ep/ep_eth.h"
#include "ep/ep_crc.h"

#include <string.h>

size_t ep_eth_build(uint8_t *buf, size_t cap, const ep_eth_hdr_t *h,
                    const uint8_t *payload, size_t len)
{
    size_t hlen = EP_ETH_HLEN + (h->vlan ? EP_ETH_VLAN_HLEN : 0);
    size_t total = hlen + len;
    size_t padded = total < EP_ETH_MIN_LEN ? EP_ETH_MIN_LEN : total;
    if (!buf || padded > cap || h->pcp > 7 || h->vid > 4095)
        return 0;
    memcpy(buf, h->dst, EP_ETH_ALEN);
    memcpy(buf + 6, h->src, EP_ETH_ALEN);
    size_t o = 12;
    if (h->vlan) {
        ep_put_be16(buf + o, EP_ETHERTYPE_VLAN);
        uint16_t tci = (uint16_t)((h->pcp << 13) | (h->dei ? 0x1000 : 0) | h->vid);
        ep_put_be16(buf + o + 2, tci);
        o += 4;
    }
    ep_put_be16(buf + o, h->ethertype);
    o += 2;
    if (len)
        memmove(buf + o, payload, len); /* payload may already sit in buf */
    if (padded > total)
        memset(buf + total, 0, padded - total);
    return padded;
}

ep_status_t ep_eth_parse(const uint8_t *frame, size_t len, ep_eth_hdr_t *out)
{
    if (!frame || len < EP_ETH_HLEN)
        return EP_ERR_FRAME;
    memset(out, 0, sizeof(*out));
    memcpy(out->dst, frame, EP_ETH_ALEN);
    memcpy(out->src, frame + 6, EP_ETH_ALEN);
    size_t o = 12;
    uint16_t type = ep_get_be16(frame + o);
    if (type == EP_ETHERTYPE_VLAN || type == EP_ETHERTYPE_QINQ) {
        if (len < EP_ETH_HLEN + EP_ETH_VLAN_HLEN)
            return EP_ERR_FRAME;
        uint16_t tci = ep_get_be16(frame + o + 2);
        out->vlan = true;
        out->pcp = (uint8_t)(tci >> 13);
        out->dei = (tci & 0x1000) != 0;
        out->vid = tci & 0x0FFF;
        o += 4;
        type = ep_get_be16(frame + o);
    }
    out->ethertype = type;
    o += 2;
    out->payload = frame + o;
    out->payload_len = len - o;
    return EP_OK;
}

size_t ep_eth_append_fcs(uint8_t *frame, size_t len, size_t cap)
{
    if (len + 4 > cap)
        return 0;
    uint32_t fcs = ep_crc32_ieee(EP_CRC32_INIT, frame, len);
    frame[len + 0] = (uint8_t)fcs;
    frame[len + 1] = (uint8_t)(fcs >> 8);
    frame[len + 2] = (uint8_t)(fcs >> 16);
    frame[len + 3] = (uint8_t)(fcs >> 24);
    return len + 4;
}

bool ep_eth_check_fcs(const uint8_t *frame, size_t len)
{
    if (len < 4)
        return false;
    /* CRC over data+FCS leaves the fixed residue 0x2144DF1C (after ~). */
    return ep_crc32_ieee(EP_CRC32_INIT, frame, len) == 0x2144DF1Cu;
}

uint64_t ep_eth_wire_time_ns(size_t frame_len_no_fcs, uint64_t link_bps)
{
    size_t l = frame_len_no_fcs < EP_ETH_MIN_LEN ? EP_ETH_MIN_LEN : frame_len_no_fcs;
    uint64_t bits = (uint64_t)(l + EP_ETH_FCS_LEN + EP_ETH_PREAMBLE_SFD + EP_ETH_IFG) * 8u;
    return (bits * 1000000000ull + link_bps - 1) / link_bps;
}

bool ep_eth_is_multicast(const uint8_t mac[EP_ETH_ALEN])
{
    return (mac[0] & 1) != 0;
}

/* --------------------------- IPv4 / UDP ---------------------------- */

uint16_t ep_inet_checksum(const uint8_t *data, size_t len, uint32_t sum)
{
    while (len > 1) {
        sum += ep_get_be16(data);
        data += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)data[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t udp_pseudo_sum(uint32_t src, uint32_t dst, uint16_t udp_len)
{
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + EP_IPPROTO_UDP + udp_len;
}

size_t ep_ipv4_udp_build(uint8_t *buf, size_t cap, const ep_udp_dgram_t *d)
{
    size_t udp_len = EP_UDP_HLEN + d->payload_len;
    size_t total = EP_IPV4_HLEN + udp_len;
    if (total > cap || total > 0xFFFF)
        return 0;
    uint8_t *ip = buf, *udp = buf + EP_IPV4_HLEN;
    if (d->payload_len)
        memmove(udp + EP_UDP_HLEN, d->payload, d->payload_len);

    ip[0] = 0x45; /* v4, IHL 5 */
    ip[1] = (uint8_t)(d->dscp << 2);
    ep_put_be16(ip + 2, (uint16_t)total);
    ep_put_be16(ip + 4, d->ip_id);
    ep_put_be16(ip + 6, 0x4000); /* DF */
    ip[8] = d->ttl ? d->ttl : 64;
    ip[9] = EP_IPPROTO_UDP;
    ep_put_be16(ip + 10, 0);
    ep_put_be32(ip + 12, d->src_ip);
    ep_put_be32(ip + 16, d->dst_ip);
    ep_put_be16(ip + 10, ep_inet_checksum(ip, EP_IPV4_HLEN, 0));

    ep_put_be16(udp + 0, d->src_port);
    ep_put_be16(udp + 2, d->dst_port);
    ep_put_be16(udp + 4, (uint16_t)udp_len);
    ep_put_be16(udp + 6, 0);
    uint16_t cs = ep_inet_checksum(udp, udp_len, udp_pseudo_sum(d->src_ip, d->dst_ip, (uint16_t)udp_len));
    ep_put_be16(udp + 6, cs ? cs : 0xFFFF); /* 0 means "no checksum" */
    return total;
}

ep_status_t ep_ipv4_udp_parse(const uint8_t *ip, size_t len, ep_udp_dgram_t *out)
{
    if (len < EP_IPV4_HLEN || (ip[0] >> 4) != 4)
        return EP_ERR_FRAME;
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    size_t total = ep_get_be16(ip + 2);
    if (ihl < EP_IPV4_HLEN || total < ihl + EP_UDP_HLEN || total > len)
        return EP_ERR_FRAME;
    if (ep_inet_checksum(ip, ihl, 0) != 0)
        return EP_ERR_CRC;
    if (ip[9] != EP_IPPROTO_UDP)
        return EP_ERR_UNSUPPORTED;
    if (ep_get_be16(ip + 6) & 0x3FFF)
        return EP_ERR_UNSUPPORTED; /* fragments not supported */
    const uint8_t *udp = ip + ihl;
    size_t udp_len = ep_get_be16(udp + 4);
    if (udp_len < EP_UDP_HLEN || ihl + udp_len > total)
        return EP_ERR_FRAME;
    memset(out, 0, sizeof(*out));
    out->src_ip = ep_get_be32(ip + 12);
    out->dst_ip = ep_get_be32(ip + 16);
    out->ttl = ip[8];
    out->dscp = ip[1] >> 2;
    out->ip_id = ep_get_be16(ip + 4);
    if (ep_get_be16(udp + 6) != 0 &&
        ep_inet_checksum(udp, udp_len, udp_pseudo_sum(out->src_ip, out->dst_ip, (uint16_t)udp_len)) != 0)
        return EP_ERR_CRC;
    out->src_port = ep_get_be16(udp);
    out->dst_port = ep_get_be16(udp + 2);
    out->payload = udp + EP_UDP_HLEN;
    out->payload_len = udp_len - EP_UDP_HLEN;
    return EP_OK;
}

/* ------------------------------ ARP -------------------------------- */

size_t ep_arp_build(uint8_t *b, size_t cap, const ep_arp_t *a)
{
    if (cap < EP_ARP_LEN)
        return 0;
    ep_put_be16(b + 0, 1);      /* HTYPE Ethernet */
    ep_put_be16(b + 2, 0x0800); /* PTYPE IPv4     */
    b[4] = 6;
    b[5] = 4;
    ep_put_be16(b + 6, a->op);
    memcpy(b + 8, a->sha, 6);
    ep_put_be32(b + 14, a->spa);
    memcpy(b + 18, a->tha, 6);
    ep_put_be32(b + 24, a->tpa);
    return EP_ARP_LEN;
}

ep_status_t ep_arp_parse(const uint8_t *b, size_t len, ep_arp_t *a)
{
    if (len < EP_ARP_LEN || ep_get_be16(b) != 1 || ep_get_be16(b + 2) != 0x0800 ||
        b[4] != 6 || b[5] != 4)
        return EP_ERR_FRAME;
    a->op = ep_get_be16(b + 6);
    memcpy(a->sha, b + 8, 6);
    a->spa = ep_get_be32(b + 14);
    memcpy(a->tha, b + 18, 6);
    a->tpa = ep_get_be32(b + 24);
    return EP_OK;
}

/* ------------------------------ PHY -------------------------------- */

ep_status_t ep_phy_reset(const ep_mdio_t *m, uint8_t phy, const ep_clock_t *clk, uint32_t timeout_ms)
{
    ep_status_t st = m->write(m->ctx, phy, EP_MII_BMCR, EP_BMCR_RESET);
    if (st != EP_OK)
        return st;
    for (uint32_t t = 0; t <= timeout_ms; t++) {
        uint16_t v;
        if ((st = m->read(m->ctx, phy, EP_MII_BMCR, &v)) != EP_OK)
            return st;
        if (!(v & EP_BMCR_RESET))
            return EP_OK; /* self-clearing bit */
        ep_clock_delay_us(clk, 1000);
    }
    return EP_ERR_TIMEOUT;
}

ep_status_t ep_phy_read_id(const ep_mdio_t *m, uint8_t phy, uint32_t *id)
{
    uint16_t a, b;
    ep_status_t st = m->read(m->ctx, phy, EP_MII_PHYID1, &a);
    if (st == EP_OK)
        st = m->read(m->ctx, phy, EP_MII_PHYID2, &b);
    if (st != EP_OK)
        return st;
    if ((a == 0xFFFF && b == 0xFFFF) || (a == 0 && b == 0))
        return EP_ERR_NACK; /* no PHY at this address */
    *id = ((uint32_t)a << 16) | b;
    return EP_OK;
}

ep_status_t ep_phy_autoneg_restart(const ep_mdio_t *m, uint8_t phy)
{
    uint16_t v;
    ep_status_t st = m->read(m->ctx, phy, EP_MII_BMCR, &v);
    if (st != EP_OK)
        return st;
    v |= EP_BMCR_ANENABLE | EP_BMCR_ANRESTART;
    v &= (uint16_t)~(EP_BMCR_LOOPBACK);
    return m->write(m->ctx, phy, EP_MII_BMCR, v);
}

ep_status_t ep_phy_force(const ep_mdio_t *m, uint8_t phy, uint32_t speed, bool fd)
{
    uint16_t v = 0;
    if (speed == 1000)
        v |= EP_BMCR_SPEED1000;
    else if (speed == 100)
        v |= EP_BMCR_SPEED100;
    else if (speed != 10)
        return EP_ERR_PARAM;
    if (fd)
        v |= EP_BMCR_FULLDPLX;
    return m->write(m->ctx, phy, EP_MII_BMCR, v);
}

ep_status_t ep_phy_get_link(const ep_mdio_t *m, uint8_t phy, ep_link_t *link)
{
    uint16_t bmcr, bmsr;
    ep_status_t st;
    memset(link, 0, sizeof(*link));
    if ((st = m->read(m->ctx, phy, EP_MII_BMCR, &bmcr)) != EP_OK)
        return st;
    /* link status is latched-low: read twice to get the current state */
    if ((st = m->read(m->ctx, phy, EP_MII_BMSR, &bmsr)) != EP_OK ||
        (st = m->read(m->ctx, phy, EP_MII_BMSR, &bmsr)) != EP_OK)
        return st;
    link->up = (bmsr & EP_BMSR_LSTATUS) != 0;
    if (!link->up)
        return EP_OK;

    if (!(bmcr & EP_BMCR_ANENABLE)) {
        link->speed_mbps = (bmcr & EP_BMCR_SPEED1000) ? 1000 : (bmcr & EP_BMCR_SPEED100) ? 100 : 10;
        link->full_duplex = (bmcr & EP_BMCR_FULLDPLX) != 0;
        return EP_OK;
    }
    if (!(bmsr & EP_BMSR_ANEGCOMPLETE)) {
        link->up = false;
        return EP_OK;
    }
    uint16_t c1000 = 0, s1000 = 0, anar, anlpar;
    m->read(m->ctx, phy, EP_MII_CTRL1000, &c1000);
    m->read(m->ctx, phy, EP_MII_STAT1000, &s1000);
    /* STAT1000 bits 11/10 = partner 1000FD/HD, CTRL1000 bits 9/8 = ours */
    uint16_t g = (uint16_t)(c1000 & (s1000 >> 2));
    if (g & 0x0200) { link->speed_mbps = 1000; link->full_duplex = true; return EP_OK; }
    if (g & 0x0100) { link->speed_mbps = 1000; link->full_duplex = false; return EP_OK; }
    if ((st = m->read(m->ctx, phy, EP_MII_ANAR, &anar)) != EP_OK ||
        (st = m->read(m->ctx, phy, EP_MII_ANLPAR, &anlpar)) != EP_OK)
        return st;
    uint16_t common = anar & anlpar;
    if (common & 0x0100)      { link->speed_mbps = 100; link->full_duplex = true; }
    else if (common & 0x0080) { link->speed_mbps = 100; link->full_duplex = false; }
    else if (common & 0x0040) { link->speed_mbps = 10;  link->full_duplex = true; }
    else                      { link->speed_mbps = 10;  link->full_duplex = false; }
    return EP_OK;
}

/* ------------------------ EtherType dispatch ----------------------- */

void ep_eth_stack_init(ep_eth_stack_t *s, const uint8_t mac[EP_ETH_ALEN])
{
    memset(s, 0, sizeof(*s));
    memcpy(s->mac, mac, EP_ETH_ALEN);
}

ep_status_t ep_eth_stack_register(ep_eth_stack_t *s, uint16_t ethertype, ep_eth_handler_t fn, void *arg)
{
    if (!fn)
        return EP_ERR_PARAM;
    if (s->n_handlers >= EP_ETH_MAX_HANDLERS)
        return EP_ERR_OVERFLOW;
    s->handlers[s->n_handlers].ethertype = ethertype;
    s->handlers[s->n_handlers].fn = fn;
    s->handlers[s->n_handlers].arg = arg;
    s->n_handlers++;
    return EP_OK;
}

void ep_eth_stack_input(ep_eth_stack_t *s, const uint8_t *frame, size_t len, uint64_t rx_ts)
{
    ep_eth_hdr_t h;
    if (ep_eth_parse(frame, len, &h) != EP_OK) {
        s->rx_errors++;
        return;
    }
    if (!ep_eth_is_multicast(h.dst) && memcmp(h.dst, s->mac, EP_ETH_ALEN) != 0) {
        s->rx_dropped_dst++;
        return;
    }
    for (size_t i = 0; i < s->n_handlers; i++) {
        if (s->handlers[i].ethertype == h.ethertype) {
            s->rx_frames++;
            s->handlers[i].fn(s->handlers[i].arg, &h, rx_ts);
            return;
        }
    }
    s->rx_dropped_type++;
}
