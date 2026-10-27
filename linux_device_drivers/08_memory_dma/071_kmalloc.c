// SPDX-License-Identifier: GPL-2.0
/*
 * 071_kmalloc.c - kmalloc family: kmalloc, kzalloc, kcalloc, krealloc, ksize.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

struct record {
	int id;
	char name[24];
};

static struct record *recs;
static char *str;

static int __init km_init(void)
{
	void *tmp;

	str = kmalloc(32, GFP_KERNEL);
	if (!str)
		return -ENOMEM;
	strscpy(str, "kmalloc'd string", 32);
	pr_info("kmalloc: '%s' usable size=%zu\n", str, ksize(str));

	recs = kcalloc(4, sizeof(*recs), GFP_KERNEL);	/* zeroed, overflow-checked */
	if (!recs)
		goto err_str;

	tmp = krealloc_array(recs, 8, sizeof(*recs), GFP_KERNEL);
	if (!tmp)
		goto err_recs;
	recs = tmp;
	recs[7].id = 7;
	strscpy(recs[7].name, "last", sizeof(recs[7].name));
	pr_info("kmalloc: recs[7] = {%d, %s}\n", recs[7].id, recs[7].name);

	tmp = kmalloc(64, GFP_ATOMIC);	/* would be used from atomic context */
	kfree(tmp);
	return 0;

err_recs:
	kfree(recs);
err_str:
	kfree(str);
	return -ENOMEM;
}

static void __exit km_exit(void)
{
	kfree(recs);
	kfree_sensitive(str);	/* zero before freeing */
}

module_init(km_init);
module_exit(km_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kmalloc family example");
