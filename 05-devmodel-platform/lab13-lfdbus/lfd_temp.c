// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_temp - driver for "temp" devices on the lfd bus (LFD430 Lab 13).
 *
 * Adds /sys/bus/lfd/devices/lfdN/temperature (millidegrees, read-only)
 * and offset (writable): temperature = 20000 + 100 * hwid + offset.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "lfdbus.h"

struct lfd_temp {
	struct lfd_device *ldev;
	int offset;
};

static ssize_t temperature_show(struct device *dev, struct device_attribute *a,
				char *buf)
{
	struct lfd_temp *t = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", 20000 + 100 * (int)t->ldev->hwid +
			  READ_ONCE(t->offset));
}
static DEVICE_ATTR_RO(temperature);

static ssize_t offset_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct lfd_temp *t = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", READ_ONCE(t->offset));
}

static ssize_t offset_store(struct device *dev, struct device_attribute *a,
			    const char *buf, size_t count)
{
	struct lfd_temp *t = dev_get_drvdata(dev);
	int v;

	if (kstrtoint(buf, 0, &v) || v < -10000 || v > 10000)
		return -EINVAL;
	WRITE_ONCE(t->offset, v);
	return count;
}
static DEVICE_ATTR_RW(offset);

static struct attribute *lfd_temp_attrs[] = {
	&dev_attr_temperature.attr,
	&dev_attr_offset.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lfd_temp);

static int lfd_temp_probe(struct lfd_device *ldev)
{
	/*
	 * TODO T5: Allocate the driver state with devm_kzalloc() on ldev->dev,
	 *   store it with dev_set_drvdata(), log "probed lfd<hwid>".  The
	 *   attributes come from .driver.dev_groups: the driver core creates
	 *   them after probe() succeeds and removes them before remove().
	 */
	return -ENOSYS;
}

static void lfd_temp_remove(struct lfd_device *ldev)
{
	dev_info(&ldev->dev, "removed\n");	/* devres frees our state */
}

static const struct lfd_device_id lfd_temp_ids[] = {
	{ .type = "temp" },
	{ }
};

static struct lfd_driver lfd_temp_driver = {
	.driver = {
		.name = "lfd_temp",
		.dev_groups = lfd_temp_groups,
	},
	.id_table = lfd_temp_ids,
	.probe = lfd_temp_probe,
	.remove = lfd_temp_remove,
};
module_lfd_driver(lfd_temp_driver);

/*
 * TODO T6: modpost cannot generate aliases for a private bus, so declare
 *   the alias that matches the modalias attribute: MODULE_ALIAS().
 */
MODULE_IMPORT_NS(LFDBUS);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temperature sensors on the lfd bus (LFD430 Lab 13)");
