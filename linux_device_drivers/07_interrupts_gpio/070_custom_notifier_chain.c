// SPDX-License-Identifier: GPL-2.0
/*
 * 070_custom_notifier_chain.c - Defining your own notifier chain plus
 * a reboot notifier.
 *
 * Drivers use notifier chains to broadcast events (e.g. "battery low")
 * to any interested subsystem.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#define EVT_BATTERY_LOW  1
#define EVT_THERMAL_TRIP 2

static BLOCKING_NOTIFIER_HEAD(training_chain);

static int listener_a(struct notifier_block *nb, unsigned long evt, void *data)
{
	pr_info("chain: A got event %lu (%s)\n", evt, (char *)data);
	return NOTIFY_OK;
}

static int listener_b(struct notifier_block *nb, unsigned long evt, void *data)
{
	pr_info("chain: B got event %lu\n", evt);
	/* stop further propagation for thermal events */
	return evt == EVT_THERMAL_TRIP ? NOTIFY_STOP : NOTIFY_OK;
}

static struct notifier_block nb_a = { .notifier_call = listener_a, .priority = 0 };
static struct notifier_block nb_b = { .notifier_call = listener_b, .priority = 10 };

static int reboot_cb(struct notifier_block *nb, unsigned long code, void *unused)
{
	pr_info("chain: system going down (code %lu), quiescing device\n", code);
	return NOTIFY_DONE;
}
static struct notifier_block reboot_nb = { .notifier_call = reboot_cb };

static int __init ch_init(void)
{
	blocking_notifier_chain_register(&training_chain, &nb_a);
	blocking_notifier_chain_register(&training_chain, &nb_b);
	register_reboot_notifier(&reboot_nb);

	blocking_notifier_call_chain(&training_chain, EVT_BATTERY_LOW, "battery 5%");
	blocking_notifier_call_chain(&training_chain, EVT_THERMAL_TRIP, "95C");
	return 0;
}

static void __exit ch_exit(void)
{
	unregister_reboot_notifier(&reboot_nb);
	blocking_notifier_chain_unregister(&training_chain, &nb_b);
	blocking_notifier_chain_unregister(&training_chain, &nb_a);
}

module_init(ch_init);
module_exit(ch_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Custom notifier chain example");
