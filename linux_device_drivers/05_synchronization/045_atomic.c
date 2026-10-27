// SPDX-License-Identifier: GPL-2.0
/*
 * 045_atomic.c - atomic_t, atomic64_t, refcount_t and atomic bit operations.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/refcount.h>
#include <linux/bitops.h>

static atomic_t a = ATOMIC_INIT(5);
static atomic64_t big = ATOMIC64_INIT(0);
static refcount_t ref = REFCOUNT_INIT(1);
static unsigned long flags;

#define FLAG_READY 0
#define FLAG_BUSY  1

static int __init at_init(void)
{
	atomic_inc(&a);
	atomic_add(10, &a);
	pr_info("atomic: a=%d\n", atomic_read(&a));
	pr_info("atomic: dec_and_test -> %d\n", atomic_sub_and_test(16, &a));
	pr_info("atomic: cmpxchg old=%d\n", atomic_cmpxchg(&a, 0, 42));

	atomic64_add(1ULL << 40, &big);
	pr_info("atomic: big=%lld\n", atomic64_read(&big));

	refcount_inc(&ref);
	pr_info("atomic: refcount=%u\n", refcount_read(&ref));
	if (!refcount_dec_and_test(&ref))
		pr_info("atomic: still referenced\n");

	set_bit(FLAG_READY, &flags);
	if (!test_and_set_bit(FLAG_BUSY, &flags))
		pr_info("atomic: acquired BUSY bit\n");
	clear_bit(FLAG_BUSY, &flags);
	pr_info("atomic: flags=0x%lx\n", flags);
	return 0;
}

static void __exit at_exit(void)
{
	pr_info("atomic: exit\n");
}

module_init(at_init);
module_exit(at_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atomic operations example");
