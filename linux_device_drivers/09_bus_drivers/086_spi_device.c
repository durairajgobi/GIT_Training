// SPDX-License-Identifier: GPL-2.0
/*
 * 086_spi_device.c - SPI protocol driver for an EEPROM-like chip.
 *
 * Device tree:
 *   eeprom@0 {
 *       compatible = "training,spi-eeprom";
 *       reg = <0>;
 *       spi-max-frequency = <1000000>;
 *   };
 */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#define CMD_READ  0x03
#define CMD_RDID  0x9f

static int se_read(struct spi_device *spi, u16 addr, u8 *buf, size_t len)
{
	u8 cmd[3] = { CMD_READ, addr >> 8, addr & 0xff };
	struct spi_transfer xfers[] = {
		{ .tx_buf = cmd, .len = sizeof(cmd) },
		{ .rx_buf = buf, .len = len },
	};

	return spi_sync_transfer(spi, xfers, ARRAY_SIZE(xfers));
}

static int se_probe(struct spi_device *spi)
{
	u8 id[3], data[16];
	int ret;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	ret = spi_write_then_read(spi, (u8[]){ CMD_RDID }, 1, id, sizeof(id));
	if (ret)
		return dev_err_probe(&spi->dev, ret, "RDID failed\n");
	dev_info(&spi->dev, "JEDEC id %3ph\n", id);

	ret = se_read(spi, 0, data, sizeof(data));
	if (!ret)
		dev_info(&spi->dev, "first bytes: %*ph\n", (int)sizeof(data), data);
	return ret;
}

static const struct of_device_id se_of_match[] = {
	{ .compatible = "training,spi-eeprom" },
	{ }
};
MODULE_DEVICE_TABLE(of, se_of_match);

static const struct spi_device_id se_ids[] = {
	{ "spi-eeprom", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, se_ids);

static struct spi_driver se_driver = {
	.driver = {
		.name = "training-spi-eeprom",
		.of_match_table = se_of_match,
	},
	.probe    = se_probe,
	.id_table = se_ids,
};
module_spi_driver(se_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPI device driver example");
