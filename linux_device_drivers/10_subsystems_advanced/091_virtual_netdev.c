// SPDX-License-Identifier: GPL-2.0
/*
 * 091_virtual_netdev.c - A virtual Ethernet device that loops TX back to RX.
 *
 * sudo ip link set vnet0 up
 * sudo ip addr add 10.10.0.1/24 dev vnet0
 * ip -s link show vnet0
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

static struct net_device *vnet;

static int vnet_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int vnet_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t vnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned int len = skb->len;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += len;

	/* loop the frame back as if it was received */
	skb_orphan(skb);
	skb->protocol = eth_type_trans(skb, dev);
	if (netif_rx(skb) == NET_RX_SUCCESS) {
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += len;
	} else {
		dev->stats.rx_dropped++;
	}
	return NETDEV_TX_OK;
}

static const struct net_device_ops vnet_ops = {
	.ndo_open            = vnet_open,
	.ndo_stop            = vnet_stop,
	.ndo_start_xmit      = vnet_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr   = eth_validate_addr,
};

static void vnet_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &vnet_ops;
	dev->flags |= IFF_NOARP;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	eth_hw_addr_random(dev);
}

static int __init vnet_init(void)
{
	int ret;

	vnet = alloc_netdev(0, "vnet%d", NET_NAME_ENUM, vnet_setup);
	if (!vnet)
		return -ENOMEM;
	ret = register_netdev(vnet);
	if (ret) {
		free_netdev(vnet);
		return ret;
	}
	netdev_info(vnet, "registered, MAC %pM\n", vnet->dev_addr);
	return 0;
}

static void __exit vnet_exit(void)
{
	unregister_netdev(vnet);
	free_netdev(vnet);
}

module_init(vnet_init);
module_exit(vnet_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual loopback Ethernet device");
