// SPDX-License-Identifier: GPL-2.0
/*
 * lfdbuf - a family of memory-backed character devices (LFD430 Lab 04).
 *
 * Each /dev/lfdbufN behaves like a small file of fixed capacity:
 *  - read()/write() at the file position, short transfers allowed;
 *  - writing at or past the capacity fails with -ENOSPC;
 *  - lseek() SEEK_END is relative to the end of the data (len), not to
 *    the capacity; positions outside [0, capacity] are -EINVAL;
 *  - open() with O_TRUNC for writing empties the device.
 * sysfs: /sys/class/lfdbuf/lfdbufN/{size,len,opens,clear}
 *
 * Module parameters: ndevs (number of devices), size (capacity in bytes).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

static unsigned int ndevs = 2;
module_param(ndevs, uint, 0444);
MODULE_PARM_DESC(ndevs, "Number of devices (1-8, default 2)");

static unsigned int size = 4096;
module_param(size, uint, 0444);
MODULE_PARM_DESC(size, "Capacity of each device in bytes (default 4096)");

#define LFDBUF_MAX_DEVS 8
#define LFDBUF_MAX_SIZE (16 * 1024 * 1024)

struct lfdbuf {
	struct cdev cdev;
	struct device *dev;
	struct mutex lock;	/* protects buf, len, opens */
	char *buf;
	size_t size;		/* capacity */
	size_t len;		/* bytes of valid data */
	unsigned long opens;
};

static dev_t lfdbuf_devt;
static struct lfdbuf *devs;

/* ------------------------------------------------------------- sysfs */
static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lfdbuf *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%zu\n", d->size);
}
static DEVICE_ATTR_RO(size);

/*
 * TODO T6: Add read-only attributes "len" and "opens" (read them under
 *   d->lock) and a write-only attribute "clear": writing "1" empties
 *   the device, anything else is -EINVAL (kstrtobool).  Use sysfs_emit
 *   and the DEVICE_ATTR_RO/WO macros.  Deck 05 slides 9-10.
 */

static struct attribute *lfdbuf_attrs[] = {
	&dev_attr_size.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lfdbuf);

static const struct class lfdbuf_class = {
	.name = "lfdbuf",
	.dev_groups = lfdbuf_groups,
};

/* ------------------------------------------------------- file operations */
static int lfdbuf_open(struct inode *inode, struct file *f)
{
	/*
	 * TODO T3: Find our struct lfdbuf from inode->i_cdev (container_of),
	 *   store it in f->private_data, count the open, and if the file is
	 *   opened for writing with O_TRUNC, empty the device (len = 0).
	 *   Deck 02 slides 10-11.
	 */
	return -ENOSYS;
}

static int lfdbuf_release(struct inode *inode, struct file *f)
{
	return 0;
}

static ssize_t lfdbuf_read(struct file *f, char __user *ubuf, size_t count,
			   loff_t *ppos)
{
	/*
	 * TODO T4: Copy at most count bytes of valid data (d->len) from
	 *   *ppos to user space under d->lock.  simple_read_from_buffer()
	 *   does the bounds checks and advances *ppos.  Deck 02 slide 12.
	 */
	return -ENOSYS;
}

static ssize_t lfdbuf_write(struct file *f, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	/*
	 * TODO T4: Copy from user space into the buffer at *ppos (capacity
	 *   d->size), extend d->len if the write went past it.  At or past
	 *   the capacity return -ENOSPC: a write() that returns 0 makes
	 *   many programs loop forever.  Writing past the end of the data
	 *   leaves a gap: it must read back as zeros, never as stale data
	 *   left over from before an O_TRUNC.  Deck 02 slide 13.
	 */
	return -ENOSYS;
}

static loff_t lfdbuf_llseek(struct file *f, loff_t off, int whence)
{
	/*
	 * TODO T5: Seek within [0, d->size], with SEEK_END relative to d->len.
	 *   generic_file_llseek_size(file, off, whence, maxsize, eof) does
	 *   exactly this; read d->len under the lock.
	 */
	return -ESPIPE;
}

static const struct file_operations lfdbuf_fops = {
	.owner = THIS_MODULE,
	.open = lfdbuf_open,
	.release = lfdbuf_release,
	.read = lfdbuf_read,
	.write = lfdbuf_write,
	.llseek = lfdbuf_llseek,
};

/* ------------------------------------------------------ setup / teardown */
static int lfdbuf_setup_one(struct lfdbuf *d, unsigned int i)
{
	dev_t devt = MKDEV(MAJOR(lfdbuf_devt), i);
	int ret;

	/*
	 * TODO T2: Initialise the device completely (mutex, buffer of `size`
	 *   bytes with kvzalloc, d->size), THEN make it live: cdev_init +
	 *   owner + cdev_add, then device_create(&lfdbuf_class, NULL, devt,
	 *   d, "lfdbuf%u", i).  Undo in reverse order on failure.  Why must
	 *   cdev_add() come after every other initialisation step?
	 *   Deck 02 slides 6, 8, 16.
	 */
	ret = -ENOSYS;
	return ret;
}

static void lfdbuf_teardown_one(struct lfdbuf *d, unsigned int i)
{
	/*
	 * TODO T2: Undo lfdbuf_setup_one() in reverse order (device_destroy
	 *   takes the dev_t).
	 */
}

static int __init lfdbuf_init(void)
{
	unsigned int i;
	int ret;

	if (!ndevs || ndevs > LFDBUF_MAX_DEVS || !size || size > LFDBUF_MAX_SIZE)
		return -EINVAL;

	/*
	 * TODO T1: Reserve ndevs device numbers with a dynamic major
	 *   (alloc_chrdev_region, name "lfdbuf"), register lfdbuf_class,
	 *   allocate the devs array, then set up each device.  Every failure
	 *   must undo exactly what was done before it.  Deck 02 slide 5.
	 */
	ret = -ENOSYS;
	return ret;
}

static void __exit lfdbuf_exit(void)
{
	/* TODO T1: Tear everything down in the reverse order of lfdbuf_init(). */
}

module_init(lfdbuf_init);
module_exit(lfdbuf_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Memory-backed character devices (LFD430 Lab 04)");
