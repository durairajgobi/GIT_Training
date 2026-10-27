// SPDX-License-Identifier: GPL-2.0
/*
 * 068_keyboard_notifier.c - Observing console keyboard events via a notifier.
 *
 * Counts key presses on the virtual console (does not log key values).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/keyboard.h>
#include <linux/notifier.h>

static unsigned long presses;

static int kbd_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	struct keyboard_notifier_param *p = data;

	if (action == KBD_KEYCODE && p->down)
		presses++;
	return NOTIFY_OK;
}

static struct notifier_block kbd_nb = {
	.notifier_call = kbd_cb,
};

static int __init kn_init(void)
{
	return register_keyboard_notifier(&kbd_nb);
}

static void __exit kn_exit(void)
{
	unregister_keyboard_notifier(&kbd_nb);
	pr_info("kbd_notifier: %lu key presses observed\n", presses);
}

module_init(kn_init);
module_exit(kn_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Keyboard notifier example");
