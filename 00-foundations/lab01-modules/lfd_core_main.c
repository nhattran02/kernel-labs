// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_core - a tiny sensor registry (LFD430 Lab 01).
 *
 * Other modules register "sensors" (an embedded struct lfd_sensor with a
 * read() callback).  The registry is exported in the LFD430 symbol
 * namespace and shown in debugfs by lfd_core_debugfs.c.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/string.h>

#include "lfd_sensor.h"
#include "lfd_core.h"

/*
 * TODO T2: Declare two module parameters:
 *     max_sensors - unsigned int, default 8, read-only in sysfs (0444)
 *     verbose     - bool, default false, writable by root (0644); the C
 *                   variable is lfd_core_verbose (see lfd_core.h), so
 *                   use module_param_named().
 *   Give both a MODULE_PARM_DESC().  Deck 00, slide 28.
 */
static unsigned int max_sensors = 8;
bool lfd_core_verbose;

DEFINE_MUTEX(lfd_core_lock);
LIST_HEAD(lfd_core_sensors);
static unsigned int nr_sensors;	/* protected by lfd_core_lock */

/**
 * lfd_sensor_register - add a sensor to the registry
 * @s: sensor; name, ops, ops->read and owner must be set
 *
 * Context: process context; takes lfd_core_lock.
 * Return: 0, -EINVAL for an incomplete sensor, -ENOSPC when max_sensors
 * sensors are already registered, -EEXIST if the name is taken.
 */
int lfd_sensor_register(struct lfd_sensor *s)
{
	/*
	 * TODO T3: Validate @s, then add it to lfd_core_sensors under the lock.
	 *   Enforce max_sensors and unique names; reset s->nreads; count in
	 *   nr_sensors.  Log "registered sensor '<name>' from <module>"
	 *   (module_name(s->owner)).  Single unlock path: use goto.
	 */
	return -ENOSYS;
}
/*
 * TODO T3: Export lfd_sensor_register, GPL-only, in namespace LFD430.
 *   v6.12 takes the namespace as a bare identifier; v6.13+ as a string.
 */

/**
 * lfd_sensor_unregister - remove a sensor added by lfd_sensor_register()
 * @s: the sensor
 *
 * After this returns, lfd_core will not call @s->ops->read() again.
 */
void lfd_sensor_unregister(struct lfd_sensor *s)
{
	/* TODO T3: Remove @s from the list under the lock and update the count. */
}
/* TODO T3: Export lfd_sensor_unregister the same way. */

static int __init lfd_core_init(void)
{
	int ret;

	ret = lfd_core_debugfs_init();
	if (ret)
		return ret;
	pr_info("ready (max_sensors=%u)\n", max_sensors);
	return 0;
}

static void __exit lfd_core_exit(void)
{
	/* Modules that use our symbols hold a reference: the list is empty. */
	WARN_ON(!list_empty(&lfd_core_sensors));
	lfd_core_debugfs_exit();
	pr_info("unloaded\n");
}

module_init(lfd_core_init);
module_exit(lfd_core_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LFD430 labs");
MODULE_DESCRIPTION("Tiny sensor registry (LFD430 Lab 01)");
