// SPDX-License-Identifier: GPL-2.0
/*
 * lfdbus - a small bus type for the LFD430 device-model lab (Lab 13).
 *
 * /sys/bus/lfd/
 *   new_device      write "<type> <hwid>"  to create device lfd<hwid>
 *   delete_device   write "<hwid>"         to remove it
 *   devices/lfdN/   attributes: type, hwid, modalias
 *   drivers/...     one directory per registered lfd_driver
 *
 * Devices hang below /sys/devices/lfdbus (a root device).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "lfdbus.h"

static struct device *lfd_root;

/* ------------------------------------------------------ device attributes */
static ssize_t type_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%s\n", to_lfd_device(dev)->type);
}
static DEVICE_ATTR_RO(type);

static ssize_t hwid_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", to_lfd_device(dev)->hwid);
}
static DEVICE_ATTR_RO(hwid);

static ssize_t modalias_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "lfd:%s\n", to_lfd_device(dev)->type);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *lfd_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_hwid.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lfd_dev);

/* ------------------------------------------------------------ bus methods */
static const struct lfd_device_id *lfd_match_id(const struct lfd_device_id *id,
						 const struct lfd_device *ldev)
{
	for (; id && id->type[0]; id++)
		if (!strcmp(id->type, ldev->type))
			return id;
	return NULL;
}

static int lfd_bus_match(struct device *dev, const struct device_driver *drv)
{
	/*
	 * TODO T1: Return 1 if the driver's id_table lists the device's type.
	 *   Deck 05 slide 8 (from registration to probe()).
	 */
	return 0;
}

static int lfd_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	/*
	 * TODO T1: Add MODALIAS=lfd:<type> and LFD_HWID=<hwid> to the uevent
	 *   with add_uevent_var().  udev (or you, with modprobe) uses
	 *   MODALIAS to load the right driver module.
	 */
	return 0;
}

static int lfd_bus_probe(struct device *dev)
{
	struct lfd_driver *ldrv = to_lfd_driver(dev->driver);

	return ldrv->probe ? ldrv->probe(to_lfd_device(dev)) : 0;
}

static void lfd_bus_remove(struct device *dev)
{
	struct lfd_driver *ldrv = to_lfd_driver(dev->driver);

	if (ldrv->remove)
		ldrv->remove(to_lfd_device(dev));
}

/* -------------------------------------------- bus attributes: hotplug */
static int lfd_device_add(const char *type, u32 hwid);

static ssize_t new_device_store(const struct bus_type *bus, const char *buf,
				size_t count)
{
	char type[LFD_TYPE_LEN];
	u32 hwid;
	int ret;

	if (sscanf(buf, "%15s %u", type, &hwid) != 2)
		return -EINVAL;
	ret = lfd_device_add(type, hwid);
	return ret ? ret : count;
}
static BUS_ATTR_WO(new_device);

static int match_hwid(struct device *dev, const void *data)
{
	return to_lfd_device(dev)->hwid == *(const u32 *)data;
}

static ssize_t delete_device_store(const struct bus_type *bus, const char *buf,
				   size_t count)
{
	struct device *dev;
	u32 hwid;

	if (kstrtou32(buf, 0, &hwid))
		return -EINVAL;
	/*
	 * TODO T4: Find the device with this hwid (bus_find_device(), which
	 *   returns a *referenced* device), unregister it, and drop the
	 *   reference you got from bus_find_device().  -ENODEV if absent.
	 */
	dev = NULL;
	return -ENOSYS;
}
static BUS_ATTR_WO(delete_device);

static struct attribute *lfd_bus_attrs[] = {
	&bus_attr_new_device.attr,
	&bus_attr_delete_device.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lfd_bus);

static const struct bus_type lfd_bus_type = {
	.name = "lfd",
	.match = lfd_bus_match,
	.uevent = lfd_bus_uevent,
	.probe = lfd_bus_probe,
	.remove = lfd_bus_remove,
	.dev_groups = lfd_dev_groups,
	.bus_groups = lfd_bus_groups,
};

/* ------------------------------------------------------------- devices */
static void lfd_device_release(struct device *dev)
{
	struct lfd_device *ldev = to_lfd_device(dev);

	pr_info("released lfd%u\n", ldev->hwid);
	kfree(ldev);
}

static int lfd_device_add(const char *type, u32 hwid)
{
	/*
	 * TODO T2: Allocate a struct lfd_device, device_initialize() it, set
	 *   bus, parent (lfd_root), release, the name "lfd<hwid>", copy type
	 *   and hwid, then device_add().  Once device_initialize() has run,
	 *   the memory belongs to the refcount: on any later error, call
	 *   put_device(), never kfree().  Deck 05 slides 5-8.
	 */
	return -ENOSYS;
}

static int unregister_one(struct device *dev, void *data)
{
	device_unregister(dev);
	return 0;
}

/* ------------------------------------------------------------- drivers */
int __lfd_driver_register(struct lfd_driver *ldrv, struct module *owner)
{
	/* TODO T3: Fill in driver.bus and driver.owner, then driver_register(). */
	return -ENOSYS;
}
EXPORT_SYMBOL_NS_GPL(__lfd_driver_register, LFDBUS);

void lfd_driver_unregister(struct lfd_driver *ldrv)
{
	driver_unregister(&ldrv->driver);
}
EXPORT_SYMBOL_NS_GPL(lfd_driver_unregister, LFDBUS);

/* ------------------------------------------------------------- module */
static int __init lfdbus_init(void)
{
	int ret;

	lfd_root = root_device_register("lfdbus");
	if (IS_ERR(lfd_root))
		return PTR_ERR(lfd_root);
	ret = bus_register(&lfd_bus_type);
	if (ret)
		root_device_unregister(lfd_root);
	return ret;
}

static void __exit lfdbus_exit(void)
{
	bus_for_each_dev(&lfd_bus_type, NULL, NULL, unregister_one);
	bus_unregister(&lfd_bus_type);
	root_device_unregister(lfd_root);
}

module_init(lfdbus_init);
module_exit(lfdbus_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A small bus type (LFD430 Lab 13)");
