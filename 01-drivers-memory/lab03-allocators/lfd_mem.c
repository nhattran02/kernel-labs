// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_mem - measure what the kernel allocators really give you (Lab 03).
 *
 * debugfs: /sys/kernel/debug/lfd_mem/
 *   sizes    requested size vs usable size of kmalloc()
 *   contig   physical contiguity of kmalloc / vmalloc / alloc_pages buffers
 *   limits   the largest kmalloc() and page-allocator requests
 *   timing   cost of one allocate+free pair per allocator
 *   cache    write "alloc N" / "free N" / "pool N"; read for counters
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/mempool.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>

static struct dentry *dir;

/* ------------------------------------------------------------------ T1 */
static const size_t sizes[] = {
	1, 8, 24, 33, 64, 96, 100, 128, 192, 200, 256, 500, 1000, 1024,
	2000, 3000, 4096, 5000, 8192, 10000,
};

static int sizes_show(struct seq_file *m, void *v)
{
	seq_puts(m, "# requested usable waste%\n");
	/*
	 * TODO T1: For every entry of sizes[], kmalloc() it, ask ksize() for
	 *   the usable size, print "%9zu %6zu %5zu\n" (requested, usable,
	 *   wasted percent of usable), and free it.  Skip failed allocations.
	 *   Deck 01 slides 33-34.
	 */
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(sizes);

/* ------------------------------------------------------------------ T2 */
/*
 * Print where @buf lives and how many times the page frame number does
 * not increase by exactly one from one page of the buffer to the next.
 */
static void contig_report(struct seq_file *m, const char *what,
			  const void *buf, size_t len)
{
	bool vm = is_vmalloc_addr(buf);
	unsigned long pfn, prev = 0;
	int breaks = 0;
	size_t off;

	/*
	 * TODO T2: For each page of the buffer (offset 0, PAGE_SIZE, ...),
	 *   get its PFN: vmalloc_to_pfn() for vmalloc addresses, and
	 *   page_to_pfn(virt_to_page()) for linear-map addresses.  Count
	 *   the places where pfn != prev + 1.  Deck 01 slides 21-25, 36.
	 */
	/* %px: this file is root-only and the point is to see raw addresses. */
	seq_printf(m, "%-12s %7zu bytes at %px (%s) pfn-breaks %d\n", what, len,
		   buf, vm ? "vmalloc area" : "linear map", breaks);
}

static int contig_show(struct seq_file *m, void *v)
{
	const size_t len = 64 * 1024;
	const unsigned int order = get_order(len);
	struct page *pg;
	void *p;

	p = kmalloc(len, GFP_KERNEL);
	if (p) {
		contig_report(m, "kmalloc", p, len);
		kfree(p);
	}
	p = vmalloc(len);
	if (p) {
		contig_report(m, "vmalloc", p, len);
		vfree(p);
	}
	pg = alloc_pages(GFP_KERNEL, order);
	if (pg) {
		contig_report(m, "alloc_pages", page_address(pg), PAGE_SIZE << order);
		__free_pages(pg, order);
	}
	p = kvmalloc(8 * 1024 * 1024, GFP_KERNEL);
	if (p) {
		contig_report(m, "kvmalloc-8M", p, 8 * 1024 * 1024);
		kvfree(p);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(contig);

/* ------------------------------------------------------------------ T3 */
static int limits_show(struct seq_file *m, void *v)
{
	struct page *pg;
	void *p;

	seq_printf(m, "PAGE_SIZE %lu MAX_PAGE_ORDER %d KMALLOC_MAX_SIZE %lu\n",
		   PAGE_SIZE, MAX_PAGE_ORDER, KMALLOC_MAX_SIZE);
	/*
	 * TODO T3: Show that kmalloc(KMALLOC_MAX_SIZE) succeeds and that
	 *   kmalloc(KMALLOC_MAX_SIZE + 1) and alloc_pages(MAX_PAGE_ORDER + 1)
	 *   fail.  Print "kmalloc(max) ok|FAILED", "kmalloc(max+1) ok|FAILED",
	 *   "alloc_pages(max_order+1) ok|FAILED".  Use __GFP_NOWARN for the
	 *   requests you expect to fail.  What would happen without it?
	 */
	p = NULL;
	pg = NULL;
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(limits);

/* ------------------------------------------------------------------ T4 */
#define TIMING_LOOPS 2000

static int timing_show(struct seq_file *m, void *v)
{
	u64 t0, ns;
	void *p;
	int i;

	seq_puts(m, "# allocator             ns/pair (TCG and KASAN inflate these)\n");
	t0 = ktime_get_ns();
	for (i = 0; i < TIMING_LOOPS; i++) {
		p = kmalloc(64, GFP_KERNEL);
		kfree(p);
	}
	ns = div_u64(ktime_get_ns() - t0, TIMING_LOOPS);
	seq_printf(m, "kmalloc(64)            %8llu\n", ns);

	/*
	 * TODO T4: Time the same loop for vmalloc(4096)/vfree() and for
	 *   alloc_page()/__free_page(), and print them in the same format.
	 *   Why is vmalloc so much more expensive than alloc_page()?
	 */
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(timing);

/* ------------------------------------------------------------------ T5/T6 */
/* The driver's request object: 200 bytes, allocated at a high rate. */
struct lfd_req {
	struct list_head node;
	spinlock_t lock;
	u32 state;
	u8 payload[176];
};

static struct kmem_cache *req_cache;
static mempool_t *req_pool;
static LIST_HEAD(held);		/* objects allocated through "alloc N" */
static DEFINE_MUTEX(held_lock);
static unsigned long nheld;
static atomic_t ctor_calls = ATOMIC_INIT(0);

#define POOL_MIN 16

/*
 * Constructor: runs when the slab allocator creates a new slab page, once
 * per object, not on every kmem_cache_alloc().  Objects must therefore be
 * returned to the cache in this same state.
 */
static void lfd_req_ctor(void *obj)
{
	struct lfd_req *r = obj;

	INIT_LIST_HEAD(&r->node);
	spin_lock_init(&r->lock);
	r->state = 0;
	atomic_inc(&ctor_calls);
}

static int cache_show(struct seq_file *m, void *v)
{
	mutex_lock(&held_lock);
	seq_printf(m, "held %lu\nctor_calls %d\nobject_size %zu\n", nheld,
		   atomic_read(&ctor_calls), sizeof(struct lfd_req));
	mutex_unlock(&held_lock);
	return 0;
}

/* Allocate up to n objects from the pool without sleeping; free them all. */
static int pool_test(unsigned long n)
{
	struct lfd_req **v;
	unsigned long i, got = 0;

	v = kcalloc(n, sizeof(*v), GFP_KERNEL);
	if (!v)
		return -ENOMEM;
	/*
	 * TODO T6: Take up to n elements with mempool_alloc(req_pool,
	 *   GFP_NOWAIT) (stop at the first NULL), log "pool: %lu of %lu
	 *   allocations succeeded", then give every element back with
	 *   mempool_free().  Why must this test not use GFP_KERNEL?
	 */
	kfree(v);
	return 0;
}

static ssize_t cache_write(struct file *f, const char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	char buf[32], op[8];
	unsigned long n, i;
	struct lfd_req *r, *tmp;
	int ret = 0;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	if (sscanf(buf, "%7s %lu", op, &n) != 2 || n > 100000)
		return -EINVAL;

	if (!strcmp(op, "pool"))
		return pool_test(n) ?: count;

	mutex_lock(&held_lock);
	if (!strcmp(op, "alloc")) {
		/*
		 * TODO T5: Allocate n objects from req_cache, check that the
		 *   constructor's state is there (state == 0, list empty),
		 *   and keep them on the "held" list.  Stop with -ENOMEM on failure.
		 */
	} else if (!strcmp(op, "free")) {
		/*
		 * TODO T5: Free up to n held objects.  Restore the constructed
		 *   state first: list_del_init() so that node is empty again.
		 */
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&held_lock);
	return ret ?: count;
}

static int cache_open(struct inode *inode, struct file *file)
{
	return single_open(file, cache_show, NULL);
}

static const struct file_operations cache_fops = {
	.owner = THIS_MODULE,
	.open = cache_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = cache_write,
};

static int __init lfd_mem_init(void)
{
	/*
	 * TODO T5: Create the "lfd_req" cache for struct lfd_req, aligned to
	 *   hardware cache lines, with lfd_req_ctor as constructor (Deck 01
	 *   slide 40).  Then create req_pool with POOL_MIN reserved objects
	 *   from that cache (mempool_create_slab_pool).  Clean up on error.
	 */

	dir = debugfs_create_dir("lfd_mem", NULL);
	debugfs_create_file("sizes", 0400, dir, NULL, &sizes_fops);
	debugfs_create_file("contig", 0400, dir, NULL, &contig_fops);
	debugfs_create_file("limits", 0400, dir, NULL, &limits_fops);
	debugfs_create_file("timing", 0400, dir, NULL, &timing_fops);
	debugfs_create_file("cache", 0600, dir, NULL, &cache_fops);
	return 0;
}

static void __exit lfd_mem_exit(void)
{
	struct lfd_req *r, *tmp;

	debugfs_remove_recursive(dir);
	list_for_each_entry_safe(r, tmp, &held, node) {
		list_del_init(&r->node);
		kmem_cache_free(req_cache, r);
	}
	/* TODO T5: Destroy the pool, then the cache (order matters: why?). */
}

module_init(lfd_mem_init);
module_exit(lfd_mem_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Allocator characterization (LFD430 Lab 03)");
