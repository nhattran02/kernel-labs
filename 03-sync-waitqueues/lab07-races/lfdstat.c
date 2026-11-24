// SPDX-License-Identifier: GPL-2.0
/*
 * lfdstat - event statistics shared by many writers (LFD430 Lab 07).
 *
 * write():  every 4-byte value written is one event.  The driver keeps
 *           count, sum, min and max, plus the most recent events as
 *           "records" (at most cfg->max_records of them).
 * ager:     a timer that runs every 20 ms in softirq context and drops
 *           records beyond the limit (the limit can shrink at any time).
 * ioctl():  GET_STATS, SET_CFG, SNAPSHOT (a cached report that SET_CFG
 *           and RESET invalidate) and RESET.  See uapi/lfdstat.h.
 *
 * The code below came from a field team.  Under load it loses events,
 * corrupts its list, reads freed memory and can deadlock.  Lab 07 asks you
 * to find every race, choose the right tool for each, and prove the fix.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/timer.h>
#include <linux/uaccess.h>

#include <uapi/lfdstat.h>

#define AGER_PERIOD_MS	20

struct lfdstat_cfg {
	u32 max_records;
	char tag[16];
	u64 generation;
};

struct record {
	struct list_head node;
	u32 value;
};

/*
 * TODO T1: Write down the locking rules before writing code: which data is
 *   shared, by which contexts (process context: write/ioctl; softirq:
 *   the ager timer), and which lock or mechanism protects what.
 *   Deck 03 slides 10-11, 18-19, 24.
 */
static struct lfdstat_stats stats;
static LIST_HEAD(records);

static struct lfdstat_cfg *cfg;
static DEFINE_MUTEX(cfg_mutex);
static u64 generation;		/* last generation handed out; cfg_mutex */

static DEFINE_MUTEX(snap_mutex);
static struct lfdstat_snapshot snap;
static bool snap_valid;

static struct timer_list ager;

/* Move records beyond @max to @dead.  Caller holds stats_lock. */
static void trim_locked(u32 max, struct list_head *dead)
{
	struct record *r;

	while (stats.nrecords > max) {
		r = list_first_entry(&records, struct record, node);
		list_move(&r->node, dead);
		stats.nrecords--;
	}
}

static void free_list(struct list_head *dead)
{
	struct record *r, *tmp;

	list_for_each_entry_safe(r, tmp, dead, node)
		kfree(r);
}

static u32 cfg_max_records(void)
{
	u32 max;

	/*
	 * TODO T4: cfg can be replaced (and the old one freed) at any moment
	 *   by SET_CFG.  Read it the RCU way.  Deck 03 slides 18-19.
	 */
	max = cfg->max_records;
	return max;
}

static int account(u32 v)
{
	struct record *rec;
	LIST_HEAD(dead);
	u32 max;

	rec = kmalloc(sizeof(*rec), GFP_KERNEL);
	if (!rec)
		return -ENOMEM;
	rec->value = v;
	max = cfg_max_records();

	/*
	 * TODO T2: Many writers run this concurrently, and the ager timer
	 *   trims the same list from softirq context.  Make the update of
	 *   stats and records one critical section.  Which lock type, and
	 *   which variant of it?  (lockdep will tell you if you are wrong.)
	 *   Never kfree() a record while another CPU may still walk it.
	 */
	stats.count++;
	stats.sum += v;
	if (stats.count == 1 || v < stats.min)
		stats.min = v;
	if (v > stats.max)
		stats.max = v;
	list_add_tail(&rec->node, &records);
	stats.nrecords++;
	trim_locked(max, &dead);
	free_list(&dead);
	return 0;
}

static void ager_fn(struct timer_list *t)
{
	LIST_HEAD(dead);
	u32 max = cfg_max_records();

	/* TODO T2: Same data as account(); this runs in softirq context. */
	trim_locked(max, &dead);
	free_list(&dead);
	mod_timer(&ager, jiffies + msecs_to_jiffies(AGER_PERIOD_MS));
}

static void get_stats(struct lfdstat_stats *s)
{
	/* TODO T2: Return a consistent copy (count and sum from the same moment). */
	*s = stats;
}

static void invalidate_snapshot(void)
{
	mutex_lock(&snap_mutex);
	snap_valid = false;
	mutex_unlock(&snap_mutex);
}

static long set_cfg(void __user *uarg)
{
	struct lfdstat_cfg_arg a;
	struct lfdstat_cfg *new, *old;

	if (copy_from_user(&a, uarg, sizeof(a)))
		return -EFAULT;
	if (!a.max_records || a.max_records > 4096 || a.pad ||
	    !memchr(a.tag, '\0', sizeof(a.tag)))
		return -EINVAL;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;
	new->max_records = a.max_records;
	strscpy(new->tag, a.tag, sizeof(new->tag));

	mutex_lock(&cfg_mutex);
	new->generation = ++generation;
	/*
	 * TODO T4: Publish the new configuration so that concurrent readers
	 *   see either the old or the new one, complete; free the old one
	 *   only after every reader that might still use it is done.
	 */
	old = cfg;
	cfg = new;
	invalidate_snapshot();		/* takes snap_mutex: cfg_mutex -> snap_mutex */
	mutex_unlock(&cfg_mutex);
	kfree(old);
	return 0;
}

static long snapshot(void __user *uarg)
{
	struct lfdstat_snapshot s;

	mutex_lock(&snap_mutex);
	if (!snap_valid) {
		get_stats(&snap.stats);
		/*
		 * TODO T5: lockdep reports a possible deadlock between this path
		 *   and set_cfg().  Draw the two lock orders, then remove the
		 *   inversion.  (Hint: does this reader need cfg_mutex at all?)
		 */
		mutex_lock(&cfg_mutex);
		memcpy(snap.tag, cfg->tag, sizeof(snap.tag));
		snap.generation = cfg->generation;
		mutex_unlock(&cfg_mutex);
		snap_valid = true;
	}
	s = snap;
	mutex_unlock(&snap_mutex);
	return copy_to_user(uarg, &s, sizeof(s)) ? -EFAULT : 0;
}

static void reset_all(void)
{
	LIST_HEAD(dead);

	list_splice_init(&records, &dead);
	memset(&stats, 0, sizeof(stats));
	free_list(&dead);
	invalidate_snapshot();
}

static ssize_t lfdstat_write(struct file *f, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	size_t done;
	u32 v;
	int ret;

	if (count % sizeof(u32))
		return -EINVAL;
	for (done = 0; done < count; done += sizeof(u32)) {
		if (get_user(v, (const u32 __user *)(ubuf + done)))
			return done ? done : -EFAULT;
		ret = account(v);
		if (ret)
			return done ? done : ret;
	}
	return count;
}

static long lfdstat_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct lfdstat_stats s;

	switch (cmd) {
	case LFDSTAT_GET_STATS:
		get_stats(&s);
		return copy_to_user(uarg, &s, sizeof(s)) ? -EFAULT : 0;
	case LFDSTAT_SET_CFG:
		return set_cfg(uarg);
	case LFDSTAT_SNAPSHOT:
		return snapshot(uarg);
	case LFDSTAT_RESET:
		reset_all();
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations lfdstat_fops = {
	.owner = THIS_MODULE,
	.write = lfdstat_write,
	.unlocked_ioctl = lfdstat_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice lfdstat_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lfdstat",
	.fops = &lfdstat_fops,
	.mode = 0666,
};

static int __init lfdstat_init(void)
{
	struct lfdstat_cfg *c;
	int ret;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->max_records = 64;
	strscpy(c->tag, "default", sizeof(c->tag));
	cfg = c;

	timer_setup(&ager, ager_fn, 0);
	mod_timer(&ager, jiffies + msecs_to_jiffies(AGER_PERIOD_MS));

	ret = misc_register(&lfdstat_misc);
	if (ret) {
		timer_delete_sync(&ager);
		kfree(c);
	}
	return ret;
}

static void __exit lfdstat_exit(void)
{
	misc_deregister(&lfdstat_misc);
	/* The ager re-arms itself: timer_shutdown_sync() stops it for good. */
	timer_shutdown_sync(&ager);
	reset_all();
	kfree(cfg);
}

module_init(lfdstat_init);
module_exit(lfdstat_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Event statistics with concurrency bugs (LFD430 Lab 07)");
