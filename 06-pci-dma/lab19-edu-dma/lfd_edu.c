// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_edu - a PCI driver for QEMU's edu device, now with DMA (Lab 19).
 *
 * Starts from the Lab 18 reference solution.  New in Lab 19:
 *   read()/write()/pread()/pwrite() move data between user memory and the
 *   device's 4 KiB buffer with the edu DMA engine, either through one
 *   coherent buffer (mode=coherent) or through streaming mappings of a
 *   fresh buffer per transfer (mode=streaming, the default).
 *
 * One /dev/eduN per device.  ioctl LFD_EDU_GET_INFO, LFD_EDU_FACT.
 * The device can disappear while files are open (unbind, hot-unplug):
 * the per-device state is reference counted and every file operation
 * checks whether the device is still there.
 *
 * Background: Deck 06 Part 2 (slides 18-28).
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
#include <linux/dma-mapping.h>
#include <linux/io-64-nonatomic-lo-hi.h>	/* writeq() on 32-bit too */
#include "edu_regs.h"
#include <uapi/lfd_edu.h>

#define DRV_NAME "lfd_edu"

static bool force_intx;
module_param(force_intx, bool, 0444);
MODULE_PARM_DESC(force_intx, "Use the INTx line even if MSI is available");

static char *mode = "streaming";
module_param(mode, charp, 0444);
MODULE_PARM_DESC(mode, "DMA buffer strategy: streaming (default) or coherent");

static unsigned int dma_bits = EDU_DMA_MASK_BITS;
module_param(dma_bits, uint, 0444);
MODULE_PARM_DESC(dma_bits, "DMA mask width to declare (default 28, what edu can do)");

/*
 * QEMU before 8.2 rejects a DMA whose last byte is the last byte of the edu
 * buffer (an off-by-one in edu_check_range(), fixed in QEMU 8.2.0) and
 * aborts the whole VM.  With the quirk on, the last byte is never used.
 */
static bool quirk_dma_end = true;
module_param(quirk_dma_end, bool, 0444);
MODULE_PARM_DESC(quirk_dma_end, "Avoid the last buffer byte (QEMU < 8.2), default 1");

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
	struct completion dma_done;
	bool coherent;
	u32 buf_size;			/* usable bytes of the device buffer */
	void *cbuf;			/* coherent mode: CPU address ... */
	dma_addr_t cbuf_dma;		/* ... and bus address of the buffer */

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
	if (status & EDU_IRQ_DMA)
		complete(&e->dma_done);
	return IRQ_HANDLED;
}


/* ----------------------------------------------------------------- DMA */
/*
 * Run one transfer between bus address @bus and offset @off of the device
 * buffer, and wait for the completion interrupt.  Caller holds e->lock.
 */
static int edu_dma(struct edu *e, dma_addr_t bus, u32 off, u32 len, bool to_ram)
{
	/*
	 * TODO T3: Program the engine (Deck 06 slide 21): SRC, DST, CNT, then
	 *   CMD = RUN | IRQ (| FROM_DEV when the device writes to RAM).
	 *   Device-side addresses are EDU_DMA_BUF + off.  The registers are
	 *   64-bit: writeq().  reinit_completion() before starting, then
	 *   wait for EDU_IRQ_DMA (1 s timeout -> -ETIMEDOUT).  Never start a
	 *   transfer outside the buffer: the model aborts QEMU (hw_error).
	 */
	return -ENOSYS;
}

/* Move @len bytes between @kbuf and device offset @off. */
static int edu_xfer(struct edu *e, void *kbuf, u32 off, u32 len, bool to_ram)
{
	struct device *dev = &e->pdev->dev;
	enum dma_data_direction dir = to_ram ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	dma_addr_t bus;
	int ret;

	if (e->coherent) {
		/*
		 * TODO T2: Coherent mode: no mapping, no syncing.  Stage the data
		 *   in e->cbuf (memcpy) and DMA from/to e->cbuf_dma.
		 */
		return -ENOSYS;
	}
	/*
	 * TODO T4: Streaming mode: dma_map_single(dev, kbuf, len, dir), check
	 *   dma_mapping_error() (-EIO), run the transfer, dma_unmap_single()
	 *   - always, also on error.  Between map and unmap the buffer
	 *   belongs to the device: the CPU must not touch it.
	 *   Deck 06 slide 20.
	 */
	(void)dev; (void)dir; (void)bus;
	return -ENOSYS;
}

/* Allocate a bounce buffer for one streaming transfer. */
static void *edu_buf_alloc(size_t len)
{
	/*
	 * TODO T5: Run the tests in streaming mode with a plain GFP_KERNEL
	 *   buffer, read the DMA-API messages in dmesg and explain them.
	 *   Then fix the allocation for a 28-bit device on a machine that
	 *   has no IOMMU and no swiotlb bounce buffers.
	 */
	return kmalloc(len, GFP_KERNEL);
}

static ssize_t edu_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
	struct edu *e = f->private_data;
	void *kbuf;
	ssize_t ret;
	u32 len;

	mutex_lock(&e->lock);
	if (!e->pdev) {
		ret = -ENODEV;
		goto out;
	}
	if (*ppos >= e->buf_size || !count) {
		ret = 0;
		goto out;
	}
	len = min_t(size_t, count, e->buf_size - *ppos);
	kbuf = edu_buf_alloc(len);
	if (!kbuf) {
		ret = -ENOMEM;
		goto out;
	}
	ret = edu_xfer(e, kbuf, *ppos, len, true);
	if (!ret)
		ret = copy_to_user(ubuf, kbuf, len) ? -EFAULT : len;
	if (ret > 0)
		*ppos += ret;
	kfree(kbuf);
out:
	mutex_unlock(&e->lock);
	return ret;
}

static ssize_t edu_write(struct file *f, const char __user *ubuf, size_t count,
			 loff_t *ppos)
{
	struct edu *e = f->private_data;
	void *kbuf;
	ssize_t ret;
	u32 len;

	mutex_lock(&e->lock);
	if (!e->pdev) {
		ret = -ENODEV;
		goto out;
	}
	if (*ppos >= e->buf_size) {
		ret = count ? -ENOSPC : 0;
		goto out;
	}
	len = min_t(size_t, count, e->buf_size - *ppos);
	kbuf = edu_buf_alloc(len);
	if (!kbuf) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(kbuf, ubuf, len))
		ret = -EFAULT;
	else
		ret = edu_xfer(e, kbuf, *ppos, len, false) ?: len;
	if (ret > 0)
		*ppos += ret;
	kfree(kbuf);
out:
	mutex_unlock(&e->lock);
	return ret;
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
	switch (cmd) {
	case LFD_EDU_GET_INFO:
		memset(&info, 0, sizeof(info));
		info.version = ioread32(e->bar + EDU_REG_ID);
		info.irq_mode = e->msi ? LFD_EDU_IRQ_MSI : LFD_EDU_IRQ_INTX;
		info.bar_size = pci_resource_len(e->pdev, 0);
		info.dma_mask = dma_get_mask(&e->pdev->dev);
		info.dma_buf_size = e->buf_size;
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
	.read = edu_read,
	.write = edu_write,
	.llseek = default_llseek,
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
	init_completion(&e->dma_done);
	e->pdev = pdev;
	e->coherent = !strcmp(mode, "coherent");
	e->buf_size = EDU_DMA_BUF_SIZE - (quirk_dma_end ? 1 : 0);

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

	/*
	 * TODO T1: Declare what the device can address: dma_bits (28 for edu)
	 *   for streaming and coherent DMA (dma_set_mask_and_coherent()).
	 *   In coherent mode allocate the 4 KiB staging buffer with
	 *   dmam_alloc_coherent() (managed: freed after remove()).
	 *   Then enable bus mastering: without it the device's DMA requests
	 *   never reach memory.  Deck 06 slides 17-19.
	 */
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

	dev_info(dev, "edu v%u.%u, %s IRQ %d, /dev/%s, %s DMA, mask %u bits\n",
		 ver >> 24, (ver >> 16) & 0xff, e->msi ? "MSI" : "INTx", e->irq, e->name,
		 e->coherent ? "coherent" : "streaming", dma_bits);
	if (e->coherent)
		dev_info(dev, "coherent buffer at bus address %pad\n", &e->cbuf_dma);
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
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCI + DMA driver for QEMU's edu device (LFD430 Lab 19)");
