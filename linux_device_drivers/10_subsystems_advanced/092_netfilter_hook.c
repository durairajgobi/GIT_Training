// SPDX-License-Identifier: GPL-2.0
/*
 * 092_netfilter_hook.c - Counting (and optionally dropping) ICMP packets.
 *
 * sudo insmod 092_netfilter_hook.ko drop_icmp=1
 * ping 127.0.0.1    # now gets no replies
 */
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>

static bool drop_icmp;
module_param(drop_icmp, bool, 0644);

static atomic64_t icmp_seen = ATOMIC64_INIT(0);

static unsigned int nf_fn(void *priv, struct sk_buff *skb,
			  const struct nf_hook_state *state)
{
	const struct iphdr *iph;

	if (!skb)
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	atomic64_inc(&icmp_seen);
	pr_info_ratelimited("nf: ICMP %pI4 -> %pI4\n", &iph->saddr, &iph->daddr);
	return READ_ONCE(drop_icmp) ? NF_DROP : NF_ACCEPT;
}

static const struct nf_hook_ops nf_ops = {
	.hook     = nf_fn,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static int __init nfh_init(void)
{
	return nf_register_net_hook(&init_net, &nf_ops);
}

static void __exit nfh_exit(void)
{
	nf_unregister_net_hook(&init_net, &nf_ops);
	pr_info("nf: saw %lld ICMP packets\n", atomic64_read(&icmp_seen));
}

module_init(nfh_init);
module_exit(nfh_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Netfilter hook example");
