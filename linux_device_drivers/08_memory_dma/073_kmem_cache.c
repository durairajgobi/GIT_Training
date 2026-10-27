// SPDX-License-Identifier: GPL-2.0
/*
 * 073_kmem_cache.c - A slab cache for frequently allocated objects.
 *
 * sudo cat /proc/slabinfo | grep training_pkt
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>

struct pkt {
	struct list_head node;
	u32 seq;
	u8 payload[200];
};

static struct kmem_cache *pkt_cache;
static LIST_HEAD(pkts);

static void pkt_ctor(void *obj)
{
	struct pkt *p = obj;

	INIT_LIST_HEAD(&p->node);
}

static int __init kc_init(void)
{
	int i;

	pkt_cache = kmem_cache_create("training_pkt", sizeof(struct pkt), 0,
				      SLAB_HWCACHE_ALIGN, pkt_ctor);
	if (!pkt_cache)
		return -ENOMEM;

	for (i = 0; i < 100; i++) {
		struct pkt *p = kmem_cache_alloc(pkt_cache, GFP_KERNEL);

		if (!p)
			break;
		p->seq = i;
		list_add_tail(&p->node, &pkts);
	}
	pr_info("kmem_cache: allocated %d packets of %zu bytes\n", i, sizeof(struct pkt));
	return 0;
}

static void __exit kc_exit(void)
{
	struct pkt *p, *tmp;

	list_for_each_entry_safe(p, tmp, &pkts, node) {
		list_del(&p->node);
		kmem_cache_free(pkt_cache, p);
	}
	kmem_cache_destroy(pkt_cache);
}

module_init(kc_init);
module_exit(kc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kmem_cache example");
