// SPDX-License-Identifier: GPL-2.0
/*
 * 097_power_supply.c - A virtual battery for the power_supply class.
 *
 * The battery slowly discharges; desktop environments show it.
 * cat /sys/class/power_supply/training_battery/capacity
 */
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

static struct power_supply *psy;
static int capacity = 100;
static struct delayed_work drain_work;

static enum power_supply_property vbat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static int vbat_get(struct power_supply *p, enum power_supply_property prop,
		    union power_supply_propval *val)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = READ_ONCE(capacity);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = 3300000 + READ_ONCE(capacity) * 9000;	/* uV */
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "Training Battery";
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc vbat_desc = {
	.name           = "training_battery",
	.type           = POWER_SUPPLY_TYPE_BATTERY,
	.properties     = vbat_props,
	.num_properties = ARRAY_SIZE(vbat_props),
	.get_property   = vbat_get,
};

static void drain_fn(struct work_struct *w)
{
	if (capacity > 0) {
		WRITE_ONCE(capacity, capacity - 1);
		power_supply_changed(psy);	/* emits a uevent */
	}
	schedule_delayed_work(&drain_work, 10 * HZ);
}

static int __init vbat_init(void)
{
	psy = power_supply_register(NULL, &vbat_desc, NULL);
	if (IS_ERR(psy))
		return PTR_ERR(psy);
	INIT_DELAYED_WORK(&drain_work, drain_fn);
	schedule_delayed_work(&drain_work, 10 * HZ);
	return 0;
}

static void __exit vbat_exit(void)
{
	cancel_delayed_work_sync(&drain_work);
	power_supply_unregister(psy);
}

module_init(vbat_init);
module_exit(vbat_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual battery power_supply example");
