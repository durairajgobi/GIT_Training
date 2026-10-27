// SPDX-License-Identifier: GPL-2.0
/*
 * 027_mmap_page.c - Sharing one kernel page with user space through mmap().
 *
 * User space:
 *   char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 *   printf("%s", p);
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/gfp.h>

static unsigned long page_addr;

static int mp_mmap(struct file *f, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn = page_to_pfn(virt_to_page((void *)page_addr));

	if (size > PAGE_SIZE || vma->vm_pgoff)
		return -EINVAL;
	return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static const struct file_operations mp_fops = {
	.owner = THIS_MODULE,
	.mmap  = mp_mmap,
};

static struct miscdevice mp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "mmap_page",
	.fops  = &mp_fops,
	.mode  = 0666,
};

static int __init mp_init(void)
{
	int ret;

	page_addr = get_zeroed_page(GFP_KERNEL);
	if (!page_addr)
		return -ENOMEM;
	snprintf((char *)page_addr, PAGE_SIZE, "Hello from a kernel page!\n");

	ret = misc_register(&mp_misc);
	if (ret)
		free_page(page_addr);
	return ret;
}

static void __exit mp_exit(void)
{
	misc_deregister(&mp_misc);
	pr_info("mmap: page now contains: %.32s\n", (char *)page_addr);
	free_page(page_addr);
}

module_init(mp_init);
module_exit(mp_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mmap a kernel page example");
