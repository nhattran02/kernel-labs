// SPDX-License-Identifier: GPL-2.0
/*
 * lfdshm - share a kernel buffer with user space (LFD430 Lab 06).
 *
 * /dev/lfdshm gives three views of one vmalloc'ed buffer:
 *   read()/write()   copies, like any character device;
 *   mmap()           the same pages mapped into the process (MAP_SHARED);
 *   ioctl FILL_USER  the kernel writes into *user* pages it has pinned.
 *
 * Module parameters:
 *   pages=N      buffer size in pages (default 16)
 *   mode=remap   map everything at mmap() time (remap_vmalloc_range)
 *   mode=fault   map lazily, one page per page fault (vm_ops->fault)
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <uapi/lfdshm.h>

static unsigned int pages = 16;
module_param(pages, uint, 0444);
MODULE_PARM_DESC(pages, "Buffer size in pages (1-1024, default 16)");

static char *mode = "remap";
module_param(mode, charp, 0444);
MODULE_PARM_DESC(mode, "remap (default) or fault");

#define LFDSHM_MAX_PIN 64

static void *buf;		/* vmalloc_user(): zeroed, may be mapped to user space */
static size_t buf_size;
static bool fault_mode;
static atomic64_t nfaults = ATOMIC64_INIT(0);

/* ------------------------------------------------------------ read/write */
static ssize_t lfdshm_read(struct file *f, char __user *ubuf, size_t count,
			   loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, count, ppos, buf, buf_size);
}

static ssize_t lfdshm_write(struct file *f, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	if (*ppos >= buf_size)
		return count ? -ENOSPC : 0;
	return simple_write_to_buffer(buf, buf_size, ppos, ubuf, count);
}

/* ------------------------------------------------------------------ mmap */
static vm_fault_t lfdshm_fault(struct vm_fault *vmf)
{
	/*
	 * TODO T3: Serve one page fault.  vmf->pgoff is the page index inside
	 *   the buffer (mmap offset included).  Out of range: VM_FAULT_SIGBUS.
	 *   Otherwise vmf->page = vmalloc_to_page(...), take a reference with
	 *   get_page() (the core mm drops it when unmapping), count the fault,
	 *   return 0.  Deck 02 slide 30.
	 */
	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct lfdshm_vm_ops = {
	.fault = lfdshm_fault,
};

static int lfdshm_mmap(struct file *f, struct vm_area_struct *vma)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;

	/*
	 * TODO T1: Only MAP_SHARED mappings make sense for a shared buffer:
	 *   reject others with -EINVAL (look for VM_SHARED in vma->vm_flags).
	 *   Reject mappings that do not fit: off + len > buf_size (careful
	 *   with overflow).  Deck 02 slides 27-29.
	 */

	if (fault_mode) {
		/*
		 * TODO T3: Do not map anything now: install lfdshm_vm_ops and mark
		 *   the VMA VM_DONTEXPAND | VM_DONTDUMP with vm_flags_set().
		 */
		return -ENODEV;
	}
	/*
	 * TODO T2: Map the whole range now with remap_vmalloc_range(vma, buf,
	 *   vma->vm_pgoff).  Why not remap_pfn_range() for vmalloc memory?
	 */
	return -ENODEV;
}

/* ----------------------------------------------------------------- ioctl */
static long fill_user(struct lfdshm_fill __user *uarg)
{
	struct lfdshm_fill fl;
	struct page **pv;
	u64 pfn[LFDSHM_MAX_PIN];
	int n, pinned, i;
	long ret = 0;

	if (copy_from_user(&fl, uarg, sizeof(fl)))
		return -EFAULT;
	if (fl.pad || !fl.len || !PAGE_ALIGNED(fl.addr) || !PAGE_ALIGNED(fl.len) ||
	    fl.len > LFDSHM_MAX_PIN * PAGE_SIZE)
		return -EINVAL;
	n = fl.len >> PAGE_SHIFT;

	pv = kcalloc(n, sizeof(*pv), GFP_KERNEL);
	if (!pv)
		return -ENOMEM;

	/*
	 * TODO T4: Pin the n user pages for writing (pin_user_pages_fast with
	 *   FOLL_WRITE).  It may pin fewer than asked: treat that as -EFAULT
	 *   and unpin what was pinned.  For each page: record page_to_pfn(),
	 *   map it with kmap_local_page(), memset() the pattern, unmap.
	 *   Release with unpin_user_pages_dirty_lock(..., true): why dirty?
	 *   Deck 02 slide 26.
	 */
	pinned = 0;
	i = 0;
	pfn[0] = 0;
	ret = -ENOTTY;
	goto out_free;
out_free:
	kfree(pv);
	return ret;
}

static long lfdshm_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct lfdshm_info info;

	switch (cmd) {
	case LFDSHM_GET_INFO:
		info.size = buf_size;
		info.faults = atomic64_read(&nfaults);
		return copy_to_user((void __user *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	case LFDSHM_FILL_USER:
		return fill_user((struct lfdshm_fill __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations lfdshm_fops = {
	.owner = THIS_MODULE,
	.read = lfdshm_read,
	.write = lfdshm_write,
	.llseek = default_llseek,
	.mmap = lfdshm_mmap,
	.unlocked_ioctl = lfdshm_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice lfdshm_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lfdshm",
	.fops = &lfdshm_fops,
	.mode = 0666,
};

static int __init lfdshm_init(void)
{
	int ret;

	if (!pages || pages > 1024)
		return -EINVAL;
	if (!strcmp(mode, "fault"))
		fault_mode = true;
	else if (strcmp(mode, "remap"))
		return -EINVAL;

	buf_size = (size_t)pages << PAGE_SHIFT;
	/*
	 * TODO T2: Allocate the buffer with vmalloc_user(): zeroed, and marked
	 *   VM_USERMAP so that remap_vmalloc_range() accepts it.
	 */
	buf = NULL;

	ret = misc_register(&lfdshm_misc);
	if (ret)
		vfree(buf);
	else
		pr_info("%zu bytes, mode %s\n", buf_size, mode);
	return ret;
}

static void __exit lfdshm_exit(void)
{
	misc_deregister(&lfdshm_misc);
	vfree(buf);
}

module_init(lfdshm_init);
module_exit(lfdshm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mmap and pinned user pages (LFD430 Lab 06)");
