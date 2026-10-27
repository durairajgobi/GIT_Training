// SPDX-License-Identifier: GPL-2.0
/*
 * 008_kernel_version.c - Compile-time and run-time kernel version checks.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/utsname.h>

static int __init kver_init(void)
{
	pr_info("kver: built for %d.%d.%d\n",
		LINUX_VERSION_CODE >> 16,
		(LINUX_VERSION_CODE >> 8) & 0xff,
		LINUX_VERSION_CODE & 0xff);
	pr_info("kver: running %s %s\n", utsname()->sysname, utsname()->release);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	pr_info("kver: class_create() takes a single argument here\n");
#else
	pr_info("kver: class_create() takes (owner, name) here\n");
#endif
	return 0;
}

static void __exit kver_exit(void)
{
	pr_info("kver: exit\n");
}

module_init(kver_init);
module_exit(kver_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel version check example");
