// SPDX-License-Identifier: GPL-2.0
/*
 * 050_percpu.c - Per-CPU counters that avoid cache-line contention.
 *
 * Each CPU increments its own counter; a reader sums all of them.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/cpumask.h>

static DEFINE_PER_CPU(unsigned long, hits);

static void bump(void *info)
{
	int i;

	for (i = 0; i < 1000; i++)
		this_cpu_inc(hits);	/* preemption-safe on the local CPU */
}

static int __init pc_init(void)
{
	unsigned long total = 0;
	int cpu;

	on_each_cpu(bump, NULL, 1);	/* run bump() on every online CPU */

	for_each_online_cpu(cpu) {
		unsigned long v = per_cpu(hits, cpu);

		pr_info("percpu: cpu%d hits=%lu\n", cpu, v);
		total += v;
	}
	pr_info("percpu: total=%lu (cpus=%u)\n", total, num_online_cpus());
	return 0;
}

static void __exit pc_exit(void)
{
	pr_info("percpu: exit\n");
}

module_init(pc_init);
module_exit(pc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Per-CPU variable example");
