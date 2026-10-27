// SPDX-License-Identifier: GPL-2.0
/*
 * 001_hello_world.c - The smallest possible loadable kernel module.
 *
 * Load:   sudo insmod 001_hello_world.ko
 * Check:  dmesg | tail
 * Unload: sudo rmmod 001_hello_world
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
	pr_info("hello: module loaded\n");
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("hello: module unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("GIT_Training");
MODULE_DESCRIPTION("Hello world kernel module");
MODULE_VERSION("1.0");
