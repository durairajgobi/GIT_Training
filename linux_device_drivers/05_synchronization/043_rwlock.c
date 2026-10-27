// SPDX-License-Identifier: GPL-2.0
/*
 * 043_rwlock.c - Reader/writer spinlock for a read-mostly lookup table.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define TABLE_SIZE 16

static DEFINE_RWLOCK(table_lock);
static int table[TABLE_SIZE];
static struct task_struct *readers[3], *writer;

static int reader_fn(void *data)
{
	long id = (long)data;

	while (!kthread_should_stop()) {
		int i, sum = 0;

		read_lock(&table_lock);		/* many readers may hold it */
		for (i = 0; i < TABLE_SIZE; i++)
			sum += table[i];
		read_unlock(&table_lock);

		pr_info_ratelimited("rwlock: reader %ld sum=%d\n", id, sum);
		msleep(200);
	}
	return 0;
}

static int writer_fn(void *data)
{
	int gen = 0;

	while (!kthread_should_stop()) {
		int i;

		write_lock(&table_lock);	/* exclusive */
		gen++;
		for (i = 0; i < TABLE_SIZE; i++)
			table[i] = gen;
		write_unlock(&table_lock);
		msleep(1000);
	}
	return 0;
}

static int __init rw_init(void)
{
	long i;

	writer = kthread_run(writer_fn, NULL, "rw_writer");
	if (IS_ERR(writer))
		return PTR_ERR(writer);
	for (i = 0; i < ARRAY_SIZE(readers); i++)
		readers[i] = kthread_run(reader_fn, (void *)i, "rw_reader%ld", i);
	return 0;
}

static void __exit rw_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(readers); i++)
		if (!IS_ERR_OR_NULL(readers[i]))
			kthread_stop(readers[i]);
	kthread_stop(writer);
}

module_init(rw_init);
module_exit(rw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rwlock example");
