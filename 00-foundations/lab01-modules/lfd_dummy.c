// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_dummy - registers fake sensors with lfd_core (LFD430 Lab 01).
 *
 *   insmod lfd_dummy.ko names=temp0,temp1,fan base=20
 *
 * Sensor i reads as base + 10 * i; "base" can be changed at run time
 * through /sys/module/lfd_dummy/parameters/base.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#include "lfd_sensor.h"

#define LFD_DUMMY_MAX 16

/*
 * TODO T5: Parameters: "names", an array of up to LFD_DUMMY_MAX strings
 *   (charp) whose element count lands in nr_names, default {"temp0",
 *   "temp1"}; and "base", an int (default 20) writable at run time.
 *   module_param_array() takes a pointer to the count (unsigned int).
 */
static char *names[LFD_DUMMY_MAX] = { "temp0", "temp1" };
static unsigned int nr_names = 2;
static int base = 20;

struct lfd_dummy {
	struct lfd_sensor sensor;
	int index;
};

static struct lfd_dummy *dummies;

static int dummy_read(struct lfd_sensor *s, int *val)
{
	struct lfd_dummy *d = container_of(s, struct lfd_dummy, sensor);

	/* base may change at any time through sysfs: read it once. */
	*val = READ_ONCE(base) + 10 * d->index;
	return 0;
}

static const struct lfd_sensor_ops dummy_ops = {
	.read = dummy_read,
};

static int __init lfd_dummy_init(void)
{
	/*
	 * TODO T5: Allocate nr_names struct lfd_dummy (reject 0 with -EINVAL),
	 *   register each sensor (owner = THIS_MODULE), and if registration
	 *   i fails, unregister 0..i-1 and free everything before returning
	 *   the error: a failed init must leave no trace.  Deck 00 slide 25.
	 */
	return -ENOSYS;
}

static void __exit lfd_dummy_exit(void)
{
	/* TODO T5: Unregister every sensor, then free the array. */
}

module_init(lfd_dummy_init);
module_exit(lfd_dummy_exit);

/* TODO T5: Import the LFD430 namespace (MODULE_IMPORT_NS). */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LFD430 labs");
MODULE_DESCRIPTION("Fake sensors for lfd_core (LFD430 Lab 01)");
