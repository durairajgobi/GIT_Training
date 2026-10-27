// SPDX-License-Identifier: GPL-2.0
/*
 * 085_i2c_client.c - I2C client driver for a temperature sensor (LM75-like).
 *
 * Instantiate from user space on bus 1 at address 0x48:
 *   echo training-lm75 0x48 > /sys/bus/i2c/devices/i2c-1/new_device
 *   cat /sys/bus/i2c/devices/1-0048/temp_mC
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/device.h>

#define REG_TEMP   0x00
#define REG_CONFIG 0x01

static ssize_t temp_mC_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 raw = i2c_smbus_read_word_swapped(client, REG_TEMP);

	if (raw < 0)
		return raw;
	/* 9-bit two's complement, 0.5 C per LSB, left-aligned in 16 bits */
	return sysfs_emit(buf, "%d\n", ((s16)raw >> 7) * 500);
}
static DEVICE_ATTR_RO(temp_mC);

static struct attribute *lm_attrs[] = {
	&dev_attr_temp_mC.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lm);

static int lm_probe(struct i2c_client *client)
{
	s32 cfg;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	cfg = i2c_smbus_read_byte_data(client, REG_CONFIG);
	if (cfg < 0)
		return dev_err_probe(&client->dev, cfg, "no chip at 0x%02x\n", client->addr);

	/* clear shutdown bit to start conversions */
	i2c_smbus_write_byte_data(client, REG_CONFIG, cfg & ~0x01);
	dev_info(&client->dev, "sensor ready, config=0x%02x\n", cfg);
	return 0;
}

static const struct i2c_device_id lm_id[] = {
	{ "training-lm75", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm_id);

static const struct of_device_id lm_of_match[] = {
	{ .compatible = "training,lm75" },
	{ }
};
MODULE_DEVICE_TABLE(of, lm_of_match);

static struct i2c_driver lm_driver = {
	.driver = {
		.name = "training-lm75",
		.of_match_table = lm_of_match,
		.dev_groups = lm_groups,
	},
	.probe    = lm_probe,
	.id_table = lm_id,
};
module_i2c_driver(lm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C client driver example");
