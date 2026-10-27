// SPDX-License-Identifier: GPL-2.0
/*
 * 022_ioctl_struct.c - Exchanging a configuration structure via ioctl.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>

struct dev_config {
	__u32 baud_rate;
	__u8  data_bits;
	__u8  parity;
	__u8  stop_bits;
	char  label[16];
};

#define CFG_MAGIC   'C'
#define CFG_GET     _IOR(CFG_MAGIC, 1, struct dev_config)
#define CFG_SET     _IOW(CFG_MAGIC, 2, struct dev_config)

static struct dev_config cfg = {
	.baud_rate = 115200, .data_bits = 8, .parity = 0, .stop_bits = 1,
	.label = "default",
};
static DEFINE_MUTEX(cfg_lock);

static long is_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct dev_config tmp;

	switch (cmd) {
	case CFG_GET:
		mutex_lock(&cfg_lock);
		tmp = cfg;
		mutex_unlock(&cfg_lock);
		return copy_to_user(uarg, &tmp, sizeof(tmp)) ? -EFAULT : 0;

	case CFG_SET:
		if (copy_from_user(&tmp, uarg, sizeof(tmp)))
			return -EFAULT;
		if (tmp.data_bits < 5 || tmp.data_bits > 8 ||
		    tmp.stop_bits < 1 || tmp.stop_bits > 2 || !tmp.baud_rate)
			return -EINVAL;
		tmp.label[sizeof(tmp.label) - 1] = '\0';
		mutex_lock(&cfg_lock);
		cfg = tmp;
		mutex_unlock(&cfg_lock);
		pr_info("cfg: %s %u %u%c%u\n", tmp.label, tmp.baud_rate,
			tmp.data_bits, "NOE"[tmp.parity % 3], tmp.stop_bits);
		return 0;
	}
	return -ENOTTY;
}

static const struct file_operations is_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = is_ioctl,
};

static struct miscdevice is_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "ioctl_struct",
	.fops  = &is_fops,
	.mode  = 0666,
};

module_misc_device(is_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ioctl with structure example");
