// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_edu - a PCI driver for QEMU's edu device, from scratch (Lab 18).
 *
 * One /dev/eduN per device.  ioctl LFD_EDU_GET_INFO, LFD_EDU_FACT.
 * The device can disappear while files are open (unbind, hot-unplug):
 * the per-device state is reference counted and every file operation
 * checks whether the device is still there.
 *
 * Background: Deck 06 Part 1 (slides 6-15); Lab 10 for the interrupt part.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/uaccess.h>
#include "edu_regs.h"
#include <uapi/lfd_edu.h>

#define DRV_NAME "lfd_edu"

static bool force_intx;
module_param(force_intx, bool, 0444);
MODULE_PARM_DESC(force_intx, "Use the INTx line even if MSI is available");

struct edu {
	struct pci_dev *pdev;		/* NULL once the device is gone */
	void __iomem *bar;
	int irq;
	bool msi;
	int id;
	char name[16];
	struct miscdevice misc;
	struct kref ref;		/* driver + one per open file */

	struct mutex lock;		/* serialises commands; protects pdev */
	struct completion fact_done;

	spinlock_t slock;		/* protects nirq */
	u64 nirq;
};

static DEFINE_IDA(edu_ida);

static void edu_free(struct kref *ref)
{
	struct edu *e = container_of(ref, struct edu, ref);

	ida_free(&edu_ida, e->id);
	kfree(e);
}

/* ------------------------------------------------------------ interrupt */
static irqreturn_t edu_isr(int irq, void *data)
{
	struct edu *e = data;
	u32 status;

	status = ioread32(e->bar + EDU_REG_IRQ_STATUS);
	if (!status)
		return IRQ_NONE;		/* shared INTx: not ours */
	iowrite32(status, e->bar + EDU_REG_IRQ_ACK);

	spin_lock(&e->slock);
	e->nirq++;
	spin_unlock(&e->slock);
	if (status & EDU_IRQ_FACT)
		complete(&e->fact_done);
	return IRQ_HANDLED;
}

/* --------------------------------------------------------- file interface */
static int edu_open(struct inode *inode, struct file *f)
{
	/* misc_open() stored our struct miscdevice in private_data. */
	struct edu *e = container_of(f->private_data, struct edu, misc);

	/*
	 * TODO T5: Every open file holds a reference on the edu state, so that
	 *   it survives the device's removal while the file is open.
	 */
	(void)e;
	return -ENOSYS;
}

static int edu_release(struct inode *inode, struct file *f)
{
	struct edu *e = f->private_data;

	/* TODO T5: Drop the file's reference. */
	(void)e;
	return 0;
}

static long edu_fact(struct edu *e, u32 __user *uarg)
{
	u32 n, r;

	if (get_user(n, uarg))
		return -EFAULT;
	reinit_completion(&e->fact_done);
	iowrite32(EDU_STATUS_IRQFACT, e->bar + EDU_REG_STATUS);
	iowrite32(n, e->bar + EDU_REG_FACT);
	if (!wait_for_completion_timeout(&e->fact_done, HZ))
		return -ETIMEDOUT;
	r = ioread32(e->bar + EDU_REG_FACT);
	iowrite32(0, e->bar + EDU_REG_STATUS);
	return put_user(r, uarg);
}

static long edu_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct edu *e = f->private_data;
	struct lfd_edu_info info;
	long ret;

	/*
	 * TODO T5: Hold e->lock for the whole command and fail with -ENODEV
	 *   once e->pdev is NULL (device removed): the BAR mapping is gone
	 *   then.  GET_INFO: ID register, IRQ mode, pci_resource_len(BAR0),
	 *   and dma_mask = 0 for now (Lab 19 sets it).  FACT: edu_fact().
	 */
	(void)e; (void)info; (void)ret; (void)edu_fact;
	return -ENOTTY;
}

static const struct file_operations edu_fops = {
	.owner = THIS_MODULE,
	.open = edu_open,
	.release = edu_release,
	.unlocked_ioctl = edu_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

/* ------------------------------------------------------------------ sysfs */
static ssize_t irq_mode_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));

	return sysfs_emit(buf, "%s\n", e->msi ? "MSI" : "INTx");
}
static DEVICE_ATTR_RO(irq_mode);

static ssize_t nirq_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));
	unsigned long flags;
	u64 n;

	spin_lock_irqsave(&e->slock, flags);
	n = e->nirq;
	spin_unlock_irqrestore(&e->slock, flags);
	return sysfs_emit(buf, "%llu\n", n);
}
static DEVICE_ATTR_RO(nirq);

static struct attribute *edu_attrs[] = {
	&dev_attr_irq_mode.attr,
	&dev_attr_nirq.attr,
	NULL,
};
ATTRIBUTE_GROUPS(edu);

/* ------------------------------------------------------------ probe/remove */
static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct edu *e;
	u32 ver;
	int ret;

	ret = ida_alloc(&edu_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;
	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		ida_free(&edu_ida, ret);
		return -ENOMEM;
	}
	e->id = ret;
	kref_init(&e->ref);		/* from here on, kref_put() frees e and id */
	mutex_init(&e->lock);
	spin_lock_init(&e->slock);
	init_completion(&e->fact_done);
	e->pdev = pdev;

	/*
	 * TODO T1: Enable the device (managed: pcim_enable_device), check that
	 *   BAR0 is a memory BAR of at least 1 MiB (pci_resource_flags/len),
	 *   map it with pcim_iomap_region(pdev, 0, DRV_NAME).  Errors go to
	 *   err_put (the kref owns e).  Deck 06 slides 8, 11.
	 */
	ret = -ENOSYS;
	goto err_put;

	/*
	 * TODO T2: Is it really an edu?  The ID register's low byte is 0xed,
	 *   and LIVENESS returns the bitwise NOT of what you write.
	 */
	ver = 0;

	pci_set_master(pdev);

	/*
	 * TODO T3: One interrupt vector: MSI preferred, INTx as fallback (or
	 *   INTx only with force_intx).  pci_alloc_irq_vectors(), then
	 *   pdev->msi_enabled tells which one you got; pci_irq_vector(pdev,
	 *   0) gives the Linux IRQ number.  devm_request_irq() with
	 *   IRQF_SHARED for INTx.  Deck 06 slide 9, Deck 04 slide 12.
	 */
	ret = -ENOSYS;
	goto err_put;

	pci_set_drvdata(pdev, e);

	/*
	 * TODO T4: Register the misc device /dev/edu<id> (MISC_DYNAMIC_MINOR,
	 *   name from e->name, edu_fops, parent = dev).  This must be the
	 *   last step: from now on user space can reach the device.
	 */
	ret = -ENOSYS;
	goto err_irq;

	dev_info(dev, "edu v%u.%u, %s IRQ %d, /dev/%s\n", ver >> 24, (ver >> 16) & 0xff,
		 e->msi ? "MSI" : "INTx", e->irq, e->name);
	return 0;

err_irq:
	devm_free_irq(dev, e->irq, e);	/* before e can go away */
err_put:
	kref_put(&e->ref, edu_free);
	return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu *e = pci_get_drvdata(pdev);

	/*
	 * TODO T5: Tear down in the reverse order of probe:
	 *    1. misc_deregister(): no new opens
	 *    2. under e->lock: e->pdev = NULL (files still open now fail)
	 *    3. silence the device (STATUS = 0)
	 *    4. devm_free_irq(): the handler's dev_id is e, and the next
	 *       step may free e.  On a shared INTx line another device's
	 *       interrupt would still call edu_isr() until devres frees it.
	 *    5. kref_put() the driver's reference
	 *   The BAR mapping and the enable are released by devres after
	 *   this function returns: in which order, and why is that safe?
	 */
	(void)e;
}

static const struct pci_device_id edu_ids[] = {
	{ PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
	.name = DRV_NAME,
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,
	.dev_groups = edu_groups,
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCI driver for QEMU's edu device (LFD430 Lab 18)");
