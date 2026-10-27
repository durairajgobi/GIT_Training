// SPDX-License-Identifier: GPL-2.0
/*
 * 074_alloc_pages.c - Page allocator: alloc_pages, page_address, kmap_local.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/highmem.h>

static unsigned int order = 2;	/* 2^order contiguous pages, 0..4 */
module_param(order, uint, 0444);

static struct page *pages;
static unsigned long addr;

static int __init ap_init(void)
{
	void *va;

	if (order > 4)
		return -EINVAL;

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!pages)
		return -ENOMEM;
	pr_info("pages: %lu pages, pfn=%lu\n", 1UL << order, page_to_pfn(pages));

	va = kmap_local_page(pages);	/* works for highmem pages too */
	memset(va, 0xAB, PAGE_SIZE);
	kunmap_local(va);

	addr = __get_free_page(GFP_KERNEL);
	if (!addr) {
		__free_pages(pages, order);
		return -ENOMEM;
	}
	pr_info("pages: single page at %px\n", (void *)addr);
	return 0;
}

static void __exit ap_exit(void)
{
	free_page(addr);
	__free_pages(pages, order);
}

module_init(ap_init);
module_exit(ap_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Page allocator example");
