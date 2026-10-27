// SPDX-License-Identifier: GPL-2.0
/*
 * 034_proc_dir_single.c - A /proc directory with single_open() entries.
 *
 * ls /proc/training/
 * cat /proc/training/status /proc/training/version
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>

static struct proc_dir_entry *dir;

static int status_show(struct seq_file *m, void *v)
{
	seq_puts(m, "state: running\n");
	seq_printf(m, "online cpus: %u\n", num_online_cpus());
	return 0;
}

static int version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", utsname()->release);
	return 0;
}

static int __init pd_init(void)
{
	dir = proc_mkdir("training", NULL);
	if (!dir)
		return -ENOMEM;

	if (!proc_create_single("status", 0444, dir, status_show) ||
	    !proc_create_single("version", 0444, dir, version_show)) {
		proc_remove(dir);
		return -ENOMEM;
	}
	return 0;
}

static void __exit pd_exit(void)
{
	proc_remove(dir);	/* removes the whole subtree */
}

module_init(pd_init);
module_exit(pd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("procfs directory with single_open entries");
