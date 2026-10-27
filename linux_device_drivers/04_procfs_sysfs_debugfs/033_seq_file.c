// SPDX-License-Identifier: GPL-2.0
/*
 * 033_seq_file.c - The seq_file iterator interface for large /proc output.
 *
 * cat /proc/training_seq   # prints "item N" for N in 0..nitems-1
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int nitems = 100;
module_param(nitems, int, 0444);

static void *sq_start(struct seq_file *s, loff_t *pos)
{
	return *pos < nitems ? pos : NULL;
}

static void *sq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return *pos < nitems ? pos : NULL;
}

static void sq_stop(struct seq_file *s, void *v)
{
}

static int sq_show(struct seq_file *s, void *v)
{
	loff_t *pos = v;

	seq_printf(s, "item %lld squared=%lld\n", *pos, *pos * *pos);
	return 0;
}

static const struct seq_operations sq_ops = {
	.start = sq_start,
	.next  = sq_next,
	.stop  = sq_stop,
	.show  = sq_show,
};

static int __init sq_init(void)
{
	return proc_create_seq("training_seq", 0444, NULL, &sq_ops) ? 0 : -ENOMEM;
}

static void __exit sq_exit(void)
{
	remove_proc_entry("training_seq", NULL);
}

module_init(sq_init);
module_exit(sq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("seq_file iterator example");
