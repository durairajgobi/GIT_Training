// SPDX-License-Identifier: GPL-2.0
/*
 * 100_netlink_socket.c - Kernel <-> user space messaging over netlink.
 *
 * A user program opens socket(AF_NETLINK, SOCK_RAW, NETLINK_TRAINING),
 * sends a message, and the kernel echoes it back in upper case.
 * (For new interfaces prefer generic netlink; raw netlink keeps this short.)
 */
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/sock.h>
#include <net/net_namespace.h>

#define NETLINK_TRAINING 31	/* unused protocol number (max is 31) */

static struct sock *nl_sk;

static void nl_recv(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	u32 portid = NETLINK_CB(skb).portid;
	struct sk_buff *out;
	struct nlmsghdr *onlh;
	char *src, *dst;
	int i, len;

	if (!nlmsg_ok(nlh, skb->len))
		return;
	src = nlmsg_data(nlh);
	len = nlmsg_len(nlh);
	pr_info("netlink: %d bytes from port %u\n", len, portid);

	out = nlmsg_new(len, GFP_KERNEL);
	if (!out)
		return;
	onlh = nlmsg_put(out, 0, nlh->nlmsg_seq, NLMSG_DONE, len, 0);
	if (!onlh) {
		kfree_skb(out);
		return;
	}
	dst = nlmsg_data(onlh);
	for (i = 0; i < len; i++)
		dst[i] = toupper(src[i]);

	nlmsg_unicast(nl_sk, out, portid);	/* consumes "out" */
}

static int __init nl_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = nl_recv,
	};

	nl_sk = netlink_kernel_create(&init_net, NETLINK_TRAINING, &cfg);
	if (!nl_sk)
		return -ENOMEM;
	pr_info("netlink: protocol %d ready\n", NETLINK_TRAINING);
	return 0;
}

static void __exit nl_exit(void)
{
	netlink_kernel_release(nl_sk);
}

module_init(nl_init);
module_exit(nl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Netlink socket example");
