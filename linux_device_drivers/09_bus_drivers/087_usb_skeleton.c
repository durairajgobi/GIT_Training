// SPDX-License-Identifier: GPL-2.0
/*
 * 087_usb_skeleton.c - USB driver exposing a bulk-IN/OUT endpoint pair as a
 * char device (/dev/trainusbN), modeled on drivers/usb/usb-skeleton.c.
 *
 * Set VENDOR_ID/PRODUCT_ID to match your device.
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define VENDOR_ID  0x1234
#define PRODUCT_ID 0x5678
#define MINOR_BASE 192

struct tusb {
	struct usb_device *udev;
	struct usb_interface *intf;
	u8 bulk_in, bulk_out;
	size_t in_size;
	u8 *in_buf;
	struct kref kref;
	struct mutex io_mutex;	/* guards intf against disconnect */
};

static struct usb_driver tusb_driver;

static void tusb_delete(struct kref *k)
{
	struct tusb *d = container_of(k, struct tusb, kref);

	usb_put_dev(d->udev);
	kfree(d->in_buf);
	kfree(d);
}

static int tusb_open(struct inode *inode, struct file *f)
{
	struct usb_interface *intf = usb_find_interface(&tusb_driver, iminor(inode));
	struct tusb *d;

	if (!intf)
		return -ENODEV;
	d = usb_get_intfdata(intf);
	if (!d)
		return -ENODEV;
	kref_get(&d->kref);
	f->private_data = d;
	return 0;
}

static int tusb_release(struct inode *inode, struct file *f)
{
	struct tusb *d = f->private_data;

	kref_put(&d->kref, tusb_delete);
	return 0;
}

static ssize_t tusb_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct tusb *d = f->private_data;
	int actual, ret;

	mutex_lock(&d->io_mutex);
	if (!d->intf) {
		ret = -ENODEV;
		goto out;
	}
	ret = usb_bulk_msg(d->udev, usb_rcvbulkpipe(d->udev, d->bulk_in),
			   d->in_buf, min(len, d->in_size), &actual, 5000);
	if (!ret)
		ret = copy_to_user(buf, d->in_buf, actual) ? -EFAULT : actual;
out:
	mutex_unlock(&d->io_mutex);
	return ret;
}

static ssize_t tusb_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	struct tusb *d = f->private_data;
	int actual, ret;
	u8 *kbuf;

	len = min_t(size_t, len, PAGE_SIZE);
	kbuf = memdup_user(buf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	mutex_lock(&d->io_mutex);
	ret = d->intf ? usb_bulk_msg(d->udev, usb_sndbulkpipe(d->udev, d->bulk_out),
				     kbuf, len, &actual, 5000) : -ENODEV;
	mutex_unlock(&d->io_mutex);
	kfree(kbuf);
	return ret ? ret : actual;
}

static const struct file_operations tusb_fops = {
	.owner   = THIS_MODULE,
	.open    = tusb_open,
	.release = tusb_release,
	.read    = tusb_read,
	.write   = tusb_write,
};

static struct usb_class_driver tusb_class = {
	.name       = "trainusb%d",
	.fops       = &tusb_fops,
	.minor_base = MINOR_BASE,
};

static int tusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *in, *out;
	struct tusb *d;
	int ret;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	kref_init(&d->kref);
	mutex_init(&d->io_mutex);
	d->udev = usb_get_dev(interface_to_usbdev(intf));
	d->intf = intf;

	ret = usb_find_common_endpoints(intf->cur_altsetting, &in, &out, NULL, NULL);
	if (ret) {
		dev_err(&intf->dev, "no bulk in/out endpoints\n");
		goto err;
	}
	d->bulk_in = in->bEndpointAddress;
	d->bulk_out = out->bEndpointAddress;
	d->in_size = usb_endpoint_maxp(in);
	d->in_buf = kmalloc(d->in_size, GFP_KERNEL);
	if (!d->in_buf) {
		ret = -ENOMEM;
		goto err;
	}

	usb_set_intfdata(intf, d);
	ret = usb_register_dev(intf, &tusb_class);
	if (ret) {
		usb_set_intfdata(intf, NULL);
		goto err;
	}
	dev_info(&intf->dev, "attached as trainusb%d\n", intf->minor - MINOR_BASE);
	return 0;
err:
	kref_put(&d->kref, tusb_delete);
	return ret;
}

static void tusb_disconnect(struct usb_interface *intf)
{
	struct tusb *d = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	usb_deregister_dev(intf, &tusb_class);

	mutex_lock(&d->io_mutex);
	d->intf = NULL;		/* stop new I/O */
	mutex_unlock(&d->io_mutex);

	kref_put(&d->kref, tusb_delete);
	dev_info(&intf->dev, "disconnected\n");
}

static const struct usb_device_id tusb_ids[] = {
	{ USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, tusb_ids);

static struct usb_driver tusb_driver = {
	.name       = "training_usb",
	.probe      = tusb_probe,
	.disconnect = tusb_disconnect,
	.id_table   = tusb_ids,
};
module_usb_driver(tusb_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB skeleton driver example");
