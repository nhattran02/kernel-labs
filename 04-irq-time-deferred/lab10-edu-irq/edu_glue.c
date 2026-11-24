// SPDX-License-Identifier: GPL-2.0
/*
 * PCI and sysfs glue for Lab 10 (provided, complete).
 *
 * Binds to QEMU's edu device (1234:11e8), maps BAR0, allocates one
 * interrupt vector (INTx by default, MSI with use_msi=1) and calls your
 * code in edu_irq.c.  You write the PCI part yourself in Lab 18.
 *
 * sysfs, in /sys/bus/pci/devices/<BDF>/:
 *   raise   (WO) write a hex value: edu_raise(value)
 *   fact    (RW) write n: compute n! with the device; read: the result
 *   stats   (RO) "irqs N none M last 0xS lat_us min A avg B max C"
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include "edu.h"

static bool use_msi;
module_param(use_msi, bool, 0444);
MODULE_PARM_DESC(use_msi, "Use MSI instead of the INTx line (default 0)");

static ssize_t raise_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct edu *e = dev_get_drvdata(dev);
	u32 v;

	if (kstrtou32(buf, 16, &v) || !v)
		return -EINVAL;
	edu_raise(e, v);
	return count;
}
static DEVICE_ATTR_WO(raise);

static u32 fact_result;		/* last result, for reading back */

static ssize_t fact_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct edu *e = dev_get_drvdata(dev);
	u32 n, r;
	int ret;

	if (kstrtou32(buf, 0, &n))
		return -EINVAL;
	ret = edu_factorial(e, n, &r);
	if (ret)
		return ret;
	WRITE_ONCE(fact_result, r);
	return count;
}

static ssize_t fact_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(fact_result));
}
static DEVICE_ATTR_RW(fact);

static ssize_t stats_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct edu *e = dev_get_drvdata(dev);
	u64 nirq, nnone, mn, mx, avg;
	unsigned long flags;
	u32 last;

	spin_lock_irqsave(&e->lock, flags);
	nirq = e->nirq;
	nnone = e->nnone;
	last = e->last_status;
	mn = e->lat_min;
	mx = e->lat_max;
	avg = e->lat_n ? div64_u64(e->lat_sum, e->lat_n) : 0;
	spin_unlock_irqrestore(&e->lock, flags);

	return sysfs_emit(buf, "irqs %llu none %llu last %#x lat_us min %llu avg %llu max %llu\n",
			  nirq, nnone, last, div_u64(mn, 1000), div_u64(avg, 1000),
			  div_u64(mx, 1000));
}
static DEVICE_ATTR_RO(stats);

static struct attribute *edu_attrs[] = {
	&dev_attr_raise.attr,
	&dev_attr_fact.attr,
	&dev_attr_stats.attr,
	NULL,
};
ATTRIBUTE_GROUPS(edu);

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct edu *e;
	u32 ver;
	int ret;

	e = devm_kzalloc(dev, sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;
	e->pdev = pdev;
	spin_lock_init(&e->lock);
	mutex_init(&e->fact_lock);
	init_completion(&e->fact_done);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	e->bar = pcim_iomap_region(pdev, 0, KBUILD_MODNAME);
	if (IS_ERR(e->bar))
		return PTR_ERR(e->bar);
	pci_set_master(pdev);		/* MSI messages are bus-master writes */

	ver = ioread32(e->bar + EDU_REG_ID);
	iowrite32(0x12345678, e->bar + EDU_REG_LIVENESS);
	if ((ver & 0xff) != 0xed || ioread32(e->bar + EDU_REG_LIVENESS) != ~0x12345678U)
		return dev_err_probe(dev, -ENODEV, "not an edu device (id %#x)\n", ver);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, use_msi ? PCI_IRQ_MSI : PCI_IRQ_INTX);
	if (ret < 0)
		return dev_err_probe(dev, ret, "no interrupt vector\n");
	e->msi = use_msi;
	e->irq = pci_irq_vector(pdev, 0);
	pci_set_drvdata(pdev, e);

	ret = edu_irq_init(e);
	if (ret)
		return ret;
	dev_info(dev, "edu v%u.%u, IRQ %d (%s)\n", ver >> 24, (ver >> 16) & 0xff,
		 e->irq, use_msi ? "MSI" : "INTx");
	return 0;
}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu *e = pci_get_drvdata(pdev);

	/* Silence the device; the IRQ and BAR are released by devres. */
	iowrite32(0, e->bar + EDU_REG_STATUS);
}

static const struct pci_device_id edu_ids[] = {
	{ PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
	.name = KBUILD_MODNAME,
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,
	.dev_groups = edu_groups,
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Interrupts on QEMU's edu device (LFD430 Lab 10)");
