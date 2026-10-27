// SPDX-License-Identifier: GPL-2.0
/*
 * 003_param_array.c - Array module parameters.
 *
 * sudo insmod 003_param_array.ko irqs=5,7,9
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#define MAX_IRQS 8

static int irqs[MAX_IRQS];
static int num_irqs;

module_param_array(irqs, int, &num_irqs, 0444);
MODULE_PARM_DESC(irqs, "List of IRQ numbers (max 8)");

static int __init parray_init(void)
{
	int i;

	pr_info("parray: %d values supplied\n", num_irqs);
	for (i = 0; i < num_irqs; i++)
		pr_info("parray: irqs[%d] = %d\n", i, irqs[i]);
	return 0;
}

static void __exit parray_exit(void)
{
	pr_info("parray: exit\n");
}

module_init(parray_init);
module_exit(parray_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Array module parameter example");
