// SPDX-License-Identifier: GPL-2.0
/*
 * 067_gpio_chip.c - A virtual GPIO controller (provider side).
 *
 * Registers an 8-line gpio_chip backed by memory. Inspect with libgpiod:
 *   gpiodetect ; gpioinfo training-gpio ; gpioset training-gpio 3=1
 */
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#define NGPIO 8

struct vgpio {
	struct gpio_chip chip;
	spinlock_t lock;
	unsigned long value;
	unsigned long dir_in;	/* bit set = input */
};

static struct platform_device *vgpio_pdev;

static int vg_get_dir(struct gpio_chip *gc, unsigned int off)
{
	struct vgpio *v = gpiochip_get_data(gc);

	return test_bit(off, &v->dir_in) ? GPIO_LINE_DIRECTION_IN
					 : GPIO_LINE_DIRECTION_OUT;
}

static int vg_dir_in(struct gpio_chip *gc, unsigned int off)
{
	struct vgpio *v = gpiochip_get_data(gc);

	set_bit(off, &v->dir_in);
	return 0;
}

static int vg_dir_out(struct gpio_chip *gc, unsigned int off, int val)
{
	struct vgpio *v = gpiochip_get_data(gc);

	clear_bit(off, &v->dir_in);
	assign_bit(off, &v->value, val);
	return 0;
}

static int vg_get(struct gpio_chip *gc, unsigned int off)
{
	struct vgpio *v = gpiochip_get_data(gc);

	return test_bit(off, &v->value);
}

static void vg_set(struct gpio_chip *gc, unsigned int off, int val)
{
	struct vgpio *v = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&v->lock, flags);
	assign_bit(off, &v->value, val);
	spin_unlock_irqrestore(&v->lock, flags);
}

static int vg_probe(struct platform_device *pdev)
{
	struct vgpio *v = devm_kzalloc(&pdev->dev, sizeof(*v), GFP_KERNEL);

	if (!v)
		return -ENOMEM;
	spin_lock_init(&v->lock);
	v->dir_in = GENMASK(NGPIO - 1, 0);

	v->chip.label            = "training-gpio";
	v->chip.parent           = &pdev->dev;
	v->chip.owner            = THIS_MODULE;
	v->chip.base             = -1;		/* dynamic numbering */
	v->chip.ngpio            = NGPIO;
	v->chip.get_direction    = vg_get_dir;
	v->chip.direction_input  = vg_dir_in;
	v->chip.direction_output = vg_dir_out;
	v->chip.get              = vg_get;
	v->chip.set              = vg_set;

	return devm_gpiochip_add_data(&pdev->dev, &v->chip, v);
}

static struct platform_driver vg_driver = {
	.probe  = vg_probe,
	.driver = { .name = "training-vgpio" },
};

static int __init vg_init(void)
{
	int ret = platform_driver_register(&vg_driver);

	if (ret)
		return ret;
	vgpio_pdev = platform_device_register_simple("training-vgpio", -1, NULL, 0);
	if (IS_ERR(vgpio_pdev)) {
		platform_driver_unregister(&vg_driver);
		return PTR_ERR(vgpio_pdev);
	}
	return 0;
}

static void __exit vg_exit(void)
{
	platform_device_unregister(vgpio_pdev);
	platform_driver_unregister(&vg_driver);
}

module_init(vg_init);
module_exit(vg_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual gpio_chip example");
