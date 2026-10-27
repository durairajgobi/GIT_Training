// SPDX-License-Identifier: GPL-2.0
/*
 * 038_debugfs_basic.c - Exposing driver internals through debugfs.
 *
 * mount -t debugfs none /sys/kernel/debug   (if not mounted)
 * ls /sys/kernel/debug/training/
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>

static struct dentry *dir;
static u32 reg_ctrl = 0x1;
static u64 rx_bytes = 123456;
static bool enabled = true;

static int __init dbg_init(void)
{
	dir = debugfs_create_dir("training", NULL);

	/* No error checks needed: debugfs helpers tolerate failure */
	debugfs_create_x32("reg_ctrl", 0644, dir, &reg_ctrl);
	debugfs_create_u64("rx_bytes", 0444, dir, &rx_bytes);
	debugfs_create_bool("enabled", 0644, dir, &enabled);
	return 0;
}

static void __exit dbg_exit(void)
{
	debugfs_remove_recursive(dir);
}

module_init(dbg_init);
module_exit(dbg_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("debugfs basic example");
