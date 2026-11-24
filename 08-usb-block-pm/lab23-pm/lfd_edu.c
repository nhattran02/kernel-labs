// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_edu - power management for the edu driver (LFD430 Lab 23).
 *
 * Starts from the Lab 18 reference solution and adds:
 *  - system sleep callbacks (suspend/resume): quiesce the device, save and
 *    restore its context;
 *  - runtime PM with autosuspend: the device is suspended when idle for
 *    AUTOSUSPEND_MS and resumed by the next ioctl.
 * The edu model has no PCI power-management capability, so the PCI core
 * keeps it in D0; the driver-side protocol is exactly the same as for a
 * device that really loses power, and that is what this lab is about.
 *
 * One /dev/eduN per device.  ioctl LFD_EDU_GET_INFO, LFD_EDU_FACT.
 * The device can disappear while files are open (unbind, hot-unplug):
 * the per-device state is reference counted and every file operation
 * checks whether the device is still there.
 *
 * Background: Deck 08 Part 2 (slides 18-24), Documentation/power/pci.rst.
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
#include <linux/pm_runtime.h>
#include "edu_regs.h"
#include <uapi/lfd_edu.h>

#define DRV_NAME "lfd_edu"
#define AUTOSUSPEND_MS 100

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

	/* Power management (Lab 23) */
	u32 saved_liveness;		/* "context" that must survive a power-down */
	atomic_t n_suspend, n_resume, n_rt_suspend, n_rt_resume;
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

	kref_get(&e->ref);
	f->private_data = e;
	return 0;
}

static int edu_release(struct inode *inode, struct file *f)
{
	struct edu *e = f->private_data;

	kref_put(&e->ref, edu_free);
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

	mutex_lock(&e->lock);
	if (!e->pdev) {
		ret = -ENODEV;
		goto out;
	}
	/*
	 * TODO T3: Hold a runtime-PM reference while the device is used:
	 *   pm_runtime_resume_and_get() here (fail the ioctl on error), and
	 *   at the end pm_runtime_mark_last_busy() +
	 *   pm_runtime_put_autosuspend().  Deck 08 slides 19-21.
	 */
	switch (cmd) {
	case LFD_EDU_GET_INFO:
		memset(&info, 0, sizeof(info));
		info.version = ioread32(e->bar + EDU_REG_ID);
		info.irq_mode = e->msi ? LFD_EDU_IRQ_MSI : LFD_EDU_IRQ_INTX;
		info.bar_size = pci_resource_len(e->pdev, 0);
		ret = copy_to_user((void __user *)arg, &info, sizeof(info)) ? -EFAULT : 0;
		break;
	case LFD_EDU_FACT:
		ret = edu_fact(e, (u32 __user *)arg);
		break;
	default:
		ret = -ENOTTY;
	}
out:
	mutex_unlock(&e->lock);
	return ret;
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


/* ---------------------------------------------------------------- PM */
/* Common "power down" / "power up" steps of system sleep and runtime PM. */
static void edu_quiesce_and_save(struct edu *e)
{
	/*
	 * TODO T1: Stop the device from raising interrupts (STATUS = 0) and
	 *   save the context that a real power-down would lose: here the
	 *   LIVENESS register (store ~its value, i.e. what was written).
	 */
}

static void edu_restore(struct edu *e)
{
	/* TODO T1: Write the saved context back. */
}

static int edu_suspend(struct device *dev)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));

	/*
	 * TODO T2: System sleep.  Take e->lock so that no ioctl is in the
	 *   middle of a command, quiesce and save, count.  The PCI core
	 *   saves the config space and picks the D-state for us.
	 */
	(void)e;
	return 0;
}

static int edu_resume(struct device *dev)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));

	(void)e;
	return 0;
}

static int edu_runtime_suspend(struct device *dev)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));

	/*
	 * TODO T3: Runtime suspend/resume: same steps, no e->lock - the PM
	 *   core never runs them while a usage reference is held, and our
	 *   ioctl holds one.  Count them.
	 */
	(void)e;
	return 0;
}

static int edu_runtime_resume(struct device *dev)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));

	(void)e;
	return 0;
}

static const struct dev_pm_ops edu_pm_ops = {
	/*
	 * TODO T2: Hook the four callbacks up with SYSTEM_SLEEP_PM_OPS() and
	 *   RUNTIME_PM_OPS().  Deck 08 slide 17.
	 */
};

static ssize_t pm_counts_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct edu *e = pci_get_drvdata(to_pci_dev(dev));

	return sysfs_emit(buf, "suspend %d resume %d rt_suspend %d rt_resume %d\n",
			  atomic_read(&e->n_suspend), atomic_read(&e->n_resume),
			  atomic_read(&e->n_rt_suspend), atomic_read(&e->n_rt_resume));
}
static DEVICE_ATTR_RO(pm_counts);

static struct attribute *edu_attrs[] = {
	&dev_attr_pm_counts.attr,
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

	ret = pcim_enable_device(pdev);
	if (ret)
		goto err_put;
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    pci_resource_len(pdev, 0) < SZ_1M) {
		ret = dev_err_probe(dev, -ENODEV, "BAR0 is not a 1 MiB memory BAR\n");
		goto err_put;
	}
	e->bar = pcim_iomap_region(pdev, 0, DRV_NAME);
	if (IS_ERR(e->bar)) {
		ret = PTR_ERR(e->bar);
		goto err_put;
	}

	ver = ioread32(e->bar + EDU_REG_ID);
	iowrite32(0xa5a5f00f, e->bar + EDU_REG_LIVENESS);
	if ((ver & 0xff) != 0xed || ioread32(e->bar + EDU_REG_LIVENESS) != ~0xa5a5f00fU) {
		ret = dev_err_probe(dev, -ENODEV, "liveness check failed (id %#x)\n", ver);
		goto err_put;
	}

	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, force_intx ? PCI_IRQ_INTX
							     : PCI_IRQ_MSI | PCI_IRQ_INTX);
	if (ret < 0) {
		dev_err_probe(dev, ret, "no IRQ vector\n");
		goto err_put;
	}
	e->msi = pdev->msi_enabled;
	e->irq = pci_irq_vector(pdev, 0);
	ret = devm_request_irq(dev, e->irq, edu_isr, e->msi ? 0 : IRQF_SHARED,
			       DRV_NAME, e);
	if (ret)
		goto err_put;

	pci_set_drvdata(pdev, e);

	snprintf(e->name, sizeof(e->name), "edu%d", e->id);
	e->misc.minor = MISC_DYNAMIC_MINOR;
	e->misc.name = e->name;
	e->misc.fops = &edu_fops;
	e->misc.parent = dev;
	e->misc.mode = 0666;
	ret = misc_register(&e->misc);
	if (ret)
		goto err_irq;

	/*
	 * TODO T4: Runtime PM for a PCI driver (Documentation/power/pci.rst):
	 *   the PCI core enabled runtime PM and holds a usage reference
	 *   across probe(); it also forbids runtime suspend until user space
	 *   writes "auto" to power/control.  Set an AUTOSUSPEND_MS
	 *   autosuspend delay, use autosuspend, and drop the core's
	 *   reference (pm_runtime_put_autosuspend).  Do not call
	 *   pm_runtime_allow() yourself.
	 */
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
	 * TODO T4: Balance probe(): pm_runtime_get_noresume(), and stop using
	 *   autosuspend.  (The core has already resumed the device.)
	 */
	misc_deregister(&e->misc);
	mutex_lock(&e->lock);
	e->pdev = NULL;
	iowrite32(0, e->bar + EDU_REG_STATUS);
	mutex_unlock(&e->lock);
	/* The handler's dev_id is e: free the IRQ before e can be freed. */
	devm_free_irq(&pdev->dev, e->irq, e);
	kref_put(&e->ref, edu_free);
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
	.driver.pm = pm_ptr(&edu_pm_ops),
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("edu driver with system sleep and runtime PM (LFD430 Lab 23)");
