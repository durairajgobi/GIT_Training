// SPDX-License-Identifier: GPL-2.0
/*
 * 069_netdev_notifier.c - Reacting to network interface events.
 *
 * sudo ip link set lo down ; sudo ip link set lo up ; dmesg | tail
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>

static int nd_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_UP:
		pr_info("netnotify: %s is UP\n", dev->name);
		break;
	case NETDEV_DOWN:
		pr_info("netnotify: %s is DOWN\n", dev->name);
		break;
	case NETDEV_REGISTER:
		pr_info("netnotify: %s registered\n", dev->name);
		break;
	case NETDEV_CHANGENAME:
		pr_info("netnotify: renamed to %s\n", dev->name);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block nd_nb = {
	.notifier_call = nd_event,
};

static int __init nn_init(void)
{
	/* replays REGISTER/UP for already existing devices */
	return register_netdevice_notifier(&nd_nb);
}

static void __exit nn_exit(void)
{
	unregister_netdevice_notifier(&nd_nb);
}

module_init(nn_init);
module_exit(nn_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Netdevice notifier example");
