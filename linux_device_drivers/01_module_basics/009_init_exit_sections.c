// SPDX-License-Identifier: GPL-2.0
/*
 * 009_init_exit_sections.c - __init, __initdata, __exit and error unwinding.
 *
 * Memory marked __init/__initdata is freed once initialization is done.
 * The init path shows the classic "goto unwind" pattern used by drivers.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

static char banner[] __initdata = "init-only banner";
static void *buf_a, *buf_b;
static bool fail_second;
module_param(fail_second, bool, 0444);
MODULE_PARM_DESC(fail_second, "Simulate failure of the second allocation");

static int __init sections_init(void)
{
	int ret;

	pr_info("sections: %s\n", banner);

	buf_a = kzalloc(128, GFP_KERNEL);
	if (!buf_a)
		return -ENOMEM;

	buf_b = fail_second ? NULL : kzalloc(256, GFP_KERNEL);
	if (!buf_b) {
		ret = -ENOMEM;
		goto err_free_a;
	}

	pr_info("sections: both buffers allocated\n");
	return 0;

err_free_a:
	kfree(buf_a);
	pr_err("sections: init failed, unwound\n");
	return ret;
}

static void __exit sections_exit(void)
{
	kfree(buf_b);
	kfree(buf_a);
	pr_info("sections: freed\n");
}

module_init(sections_init);
module_exit(sections_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("__init/__exit and error unwinding example");
