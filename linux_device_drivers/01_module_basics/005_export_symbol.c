// SPDX-License-Identifier: GPL-2.0
/*
 * 005_export_symbol.c - Exporting a function and variable for other modules.
 *
 * Other modules can declare:
 *   extern int training_add(int a, int b);
 * and call it once this module is loaded.
 * See: grep training_ /proc/kallsyms
 */
#include <linux/init.h>
#include <linux/module.h>

/* Normally these prototypes live in a shared header. */
extern int training_counter;
int training_add(int a, int b);

int training_counter;
EXPORT_SYMBOL_GPL(training_counter);

int training_add(int a, int b)
{
	training_counter++;
	return a + b;
}
EXPORT_SYMBOL_GPL(training_add);

static int __init exp_init(void)
{
	pr_info("export: 2 + 3 = %d\n", training_add(2, 3));
	return 0;
}

static void __exit exp_exit(void)
{
	pr_info("export: called %d times\n", training_counter);
}

module_init(exp_init);
module_exit(exp_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EXPORT_SYMBOL example");
