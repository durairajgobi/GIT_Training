// SPDX-License-Identifier: GPL-2.0
/*
 * 089_input_device.c - A virtual input device that generates key events.
 *
 * Writing a character to /dev/vkeys injects KEY_A..KEY_Z / KEY_1..KEY_0.
 * Watch with: sudo evtest   (pick "Training Virtual Keys")
 */
#include <linux/module.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

static struct input_dev *idev;

static const unsigned short letter_keys[26] = {
	KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
	KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
	KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
};

static int char_to_key(char c)
{
	if (c >= 'a' && c <= 'z')
		return letter_keys[c - 'a'];
	if (c >= '1' && c <= '9')
		return KEY_1 + (c - '1');
	if (c == '0')
		return KEY_0;
	if (c == ' ')
		return KEY_SPACE;
	if (c == '\n')
		return KEY_ENTER;
	return -1;
}

static ssize_t vk_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	size_t i;
	char c;

	for (i = 0; i < len; i++) {
		int key;

		if (get_user(c, buf + i))
			return -EFAULT;
		key = char_to_key(c);
		if (key < 0)
			continue;
		input_report_key(idev, key, 1);
		input_sync(idev);
		input_report_key(idev, key, 0);
		input_sync(idev);
	}
	return len;
}

static const struct file_operations vk_fops = {
	.owner = THIS_MODULE,
	.write = vk_write,
};

static struct miscdevice vk_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "vkeys",
	.fops  = &vk_fops,
	.mode  = 0600,
};

static int __init vk_init(void)
{
	int i, ret;

	idev = input_allocate_device();
	if (!idev)
		return -ENOMEM;
	idev->name = "Training Virtual Keys";
	idev->id.bustype = BUS_VIRTUAL;

	__set_bit(EV_KEY, idev->evbit);
	for (i = 0; i < ARRAY_SIZE(letter_keys); i++)
		__set_bit(letter_keys[i], idev->keybit);
	for (i = KEY_1; i <= KEY_0; i++)
		__set_bit(i, idev->keybit);
	__set_bit(KEY_SPACE, idev->keybit);
	__set_bit(KEY_ENTER, idev->keybit);

	ret = input_register_device(idev);
	if (ret) {
		input_free_device(idev);
		return ret;
	}
	ret = misc_register(&vk_misc);
	if (ret)
		input_unregister_device(idev);
	return ret;
}

static void __exit vk_exit(void)
{
	misc_deregister(&vk_misc);
	input_unregister_device(idev);	/* also frees it */
}

module_init(vk_init);
module_exit(vk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual input device example");
