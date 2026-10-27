// SPDX-License-Identifier: GPL-2.0
/*
 * 072_vmalloc.c - vmalloc vs kvmalloc for large buffers.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/mm.h>

static unsigned long size_mb = 16;
module_param(size_mb, ulong, 0444);

static void *vbuf, *kvbuf;

static int __init vm_init(void)
{
	vbuf = vzalloc(size_mb << 20);
	if (!vbuf)
		return -ENOMEM;
	pr_info("vmalloc: %lu MB at %px, is_vmalloc=%d\n",
		size_mb, vbuf, is_vmalloc_addr(vbuf));

	/* tries kmalloc first, falls back to vmalloc for big sizes */
	kvbuf = kvmalloc(256 * 1024, GFP_KERNEL);
	if (!kvbuf) {
		vfree(vbuf);
		return -ENOMEM;
	}
	pr_info("vmalloc: kvmalloc chose %s\n",
		is_vmalloc_addr(kvbuf) ? "vmalloc" : "kmalloc");
	return 0;
}

static void __exit vm_exit(void)
{
	kvfree(kvbuf);
	vfree(vbuf);
}

module_init(vm_init);
module_exit(vm_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("vmalloc / kvmalloc example");
