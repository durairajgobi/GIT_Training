// SPDX-License-Identifier: GPL-2.0
/*
 * 075_mempool.c - Guaranteed allocations with a mempool.
 *
 * A mempool keeps a reserve of pre-allocated objects so critical paths
 * (e.g. block I/O) can make progress even under memory pressure.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/slab.h>

#define MIN_RESERVED 8

struct io_req {
	u64 sector;
	u32 len;
};

static struct kmem_cache *req_cache;
static mempool_t *pool;

static int __init mp_init(void)
{
	struct io_req *reqs[MIN_RESERVED];
	int i;

	req_cache = KMEM_CACHE(io_req, 0);
	if (!req_cache)
		return -ENOMEM;

	pool = mempool_create_slab_pool(MIN_RESERVED, req_cache);
	if (!pool) {
		kmem_cache_destroy(req_cache);
		return -ENOMEM;
	}

	for (i = 0; i < MIN_RESERVED; i++) {
		reqs[i] = mempool_alloc(pool, GFP_NOIO);	/* never fails when sleeping allowed */
		reqs[i]->sector = i * 8;
		reqs[i]->len = 4096;
	}
	pr_info("mempool: allocated %d requests, last sector=%llu\n",
		MIN_RESERVED, reqs[MIN_RESERVED - 1]->sector);
	for (i = 0; i < MIN_RESERVED; i++)
		mempool_free(reqs[i], pool);
	return 0;
}

static void __exit mp_exit(void)
{
	mempool_destroy(pool);
	kmem_cache_destroy(req_cache);
}

module_init(mp_init);
module_exit(mp_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mempool example");
