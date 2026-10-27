// SPDX-License-Identifier: GPL-2.0
/*
 * 079_ioremap_mmio.c - Mapping and accessing memory-mapped registers.
 *
 * Device tree:
 *   uart0: serial@10000000 {
 *       compatible = "training,mmio-demo";
 *       reg = <0x10000000 0x100>;
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/bitfield.h>

#define REG_ID      0x00
#define REG_CTRL    0x04
#define REG_STATUS  0x08
#define CTRL_ENABLE BIT(0)
#define CTRL_MODE   GENMASK(3, 1)

struct mmio_priv {
	void __iomem *base;
};

static int mmio_probe(struct platform_device *pdev)
{
	struct mmio_priv *p;
	u32 ctrl;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	/* request_mem_region + ioremap in one managed call */
	p->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(p->base))
		return PTR_ERR(p->base);

	dev_info(&pdev->dev, "ID register = 0x%08x\n", readl(p->base + REG_ID));

	ctrl = readl(p->base + REG_CTRL);
	ctrl &= ~CTRL_MODE;
	ctrl |= FIELD_PREP(CTRL_MODE, 3) | CTRL_ENABLE;
	writel(ctrl, p->base + REG_CTRL);

	/* relaxed accessors skip barriers - use for hot paths with care */
	dev_info(&pdev->dev, "status = 0x%08x\n", readl_relaxed(p->base + REG_STATUS));

	platform_set_drvdata(pdev, p);
	return 0;
}

static const struct of_device_id mmio_of_match[] = {
	{ .compatible = "training,mmio-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, mmio_of_match);

static struct platform_driver mmio_driver = {
	.probe = mmio_probe,
	.driver = {
		.name = "training-mmio",
		.of_match_table = mmio_of_match,
	},
};
module_platform_driver(mmio_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ioremap / MMIO register access example");
