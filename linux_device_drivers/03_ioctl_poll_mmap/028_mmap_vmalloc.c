// SPDX-License-Identifier: GPL-2.0
/*
 * 028_mmap_vmalloc.c - mmap() of a multi-page vmalloc buffer.
 *
 * vmalloc memory is not physically contiguous, so remap_vmalloc_range()
 * is used instead of remap_pfn_range().
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#define NPAGES 16

static void *vbuf;

static int mv_mmap(struct file *f, struct vm_area_struct *vma)
{
	if (vma->vm_end - vma->vm_start > NPAGES * PAGE_SIZE)
		return -EINVAL;
	return remap_vmalloc_range(vma, vbuf, vma->vm_pgoff);
}

static const struct file_operations mv_fops = {
	.owner = THIS_MODULE,
	.mmap  = mv_mmap,
};

static struct miscdevice mv_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "mmap_vmalloc",
	.fops  = &mv_fops,
	.mode  = 0666,
};

static int __init mv_init(void)
{
	int ret, i;

	vbuf = vmalloc_user(NPAGES * PAGE_SIZE);	/* zeroed, mmap-able */
	if (!vbuf)
		return -ENOMEM;
	for (i = 0; i < NPAGES; i++)
		snprintf(vbuf + i * PAGE_SIZE, PAGE_SIZE, "page %d\n", i);

	ret = misc_register(&mv_misc);
	if (ret)
		vfree(vbuf);
	return ret;
}

static void __exit mv_exit(void)
{
	misc_deregister(&mv_misc);
	vfree(vbuf);
}

module_init(mv_init);
module_exit(mv_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mmap a vmalloc buffer example");
