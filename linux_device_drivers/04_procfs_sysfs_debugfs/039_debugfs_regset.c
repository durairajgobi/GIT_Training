// SPDX-License-Identifier: GPL-2.0
/*
 * 039_debugfs_regset.c - Dumping a (simulated) register bank via debugfs.
 *
 * cat /sys/kernel/debug/training_regs/regs
 * cat /sys/kernel/debug/training_regs/blob | hexdump -C
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/slab.h>

#define NREGS 8

static struct dentry *dir;
static u32 *regs;		/* stands in for an ioremap()ed register block */
static struct debugfs_regset32 regset;
static struct debugfs_blob_wrapper blob;

static const struct debugfs_reg32 reg_names[] = {
	{ "CTRL",   0x00 }, { "STATUS", 0x04 }, { "IRQ_EN", 0x08 },
	{ "IRQ_ST", 0x0c }, { "DATA",   0x10 }, { "CFG0",   0x14 },
	{ "CFG1",   0x18 }, { "VER",    0x1c },
};

static int __init rs_init(void)
{
	int i;

	regs = kcalloc(NREGS, sizeof(u32), GFP_KERNEL);
	if (!regs)
		return -ENOMEM;
	for (i = 0; i < NREGS; i++)
		regs[i] = 0xA5000000 | i;

	regset.regs  = reg_names;
	regset.nregs = ARRAY_SIZE(reg_names);
	regset.base  = (void __iomem *)regs;	/* demo only - real code uses ioremap */

	blob.data = regs;
	blob.size = NREGS * sizeof(u32);

	dir = debugfs_create_dir("training_regs", NULL);
	debugfs_create_regset32("regs", 0444, dir, &regset);
	debugfs_create_blob("blob", 0444, dir, &blob);
	return 0;
}

static void __exit rs_exit(void)
{
	debugfs_remove_recursive(dir);
	kfree(regs);
}

module_init(rs_init);
module_exit(rs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("debugfs regset32 and blob example");
