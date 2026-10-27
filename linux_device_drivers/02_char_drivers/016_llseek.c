// SPDX-License-Identifier: GPL-2.0
/*
 * 016_llseek.c - Implementing llseek for a fixed-size memory device.
 *
 * printf 'ABCDEFGH' > /dev/seekdev
 * dd if=/dev/seekdev bs=1 skip=4 count=4 2>/dev/null   # -> EFGH
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#define MEM_SIZE 4096

static char mem[MEM_SIZE];

static loff_t sk_llseek(struct file *f, loff_t offset, int whence)
{
	/* fixed_size_llseek handles SEEK_SET/SEEK_CUR/SEEK_END with bounds */
	return fixed_size_llseek(f, offset, whence, MEM_SIZE);
}

static ssize_t sk_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	return simple_read_from_buffer(buf, len, off, mem, MEM_SIZE);
}

static ssize_t sk_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	return simple_write_to_buffer(mem, MEM_SIZE, off, buf, len);
}

static const struct file_operations sk_fops = {
	.owner  = THIS_MODULE,
	.llseek = sk_llseek,
	.read   = sk_read,
	.write  = sk_write,
};

static struct miscdevice sk_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "seekdev",
	.fops  = &sk_fops,
	.mode  = 0666,
};

module_misc_device(sk_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("llseek example");
