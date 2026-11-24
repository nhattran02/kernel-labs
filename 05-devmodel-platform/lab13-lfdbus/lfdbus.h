/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The "LFD sensor fabric" bus (LFD430 Lab 13).
 *
 * Devices have a type string ("temp", "humid", ...) and a numeric hardware
 * id.  Drivers list the types they handle; the bus core matches them.
 */
#ifndef LFDBUS_H
#define LFDBUS_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>

#define LFD_TYPE_LEN 16

struct lfd_device_id {
	char type[LFD_TYPE_LEN];
};

struct lfd_device {
	struct device dev;
	char type[LFD_TYPE_LEN];
	u32 hwid;
};
#define to_lfd_device(d) container_of(d, struct lfd_device, dev)

struct lfd_driver {
	struct device_driver driver;
	const struct lfd_device_id *id_table;	/* terminated by an empty type */
	int (*probe)(struct lfd_device *ldev);
	void (*remove)(struct lfd_device *ldev);
};
#define to_lfd_driver(d) container_of_const(d, struct lfd_driver, driver)

int __lfd_driver_register(struct lfd_driver *ldrv, struct module *owner);
#define lfd_driver_register(ldrv) __lfd_driver_register(ldrv, THIS_MODULE)
void lfd_driver_unregister(struct lfd_driver *ldrv);

/* One-line module boilerplate, like module_platform_driver(). */
#define module_lfd_driver(__drv) \
	module_driver(__drv, lfd_driver_register, lfd_driver_unregister)

#endif /* LFDBUS_H */
