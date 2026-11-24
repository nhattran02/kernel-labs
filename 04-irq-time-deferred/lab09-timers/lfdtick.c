// SPDX-License-Identifier: GPL-2.0
/*
 * lfdtick - measure kernel clocks, timers and delays (LFD430 Lab 09).
 *
 * debugfs: /sys/kernel/debug/lfdtick/
 *   clocks    HZ, jiffies and the main clocks, cost of ktime_get_ns()
 *   run       write one command; it blocks until the measurement is done:
 *               timer <period_ms> <n>     periodic timer_list
 *               hrtimer <period_us> <n>   periodic hrtimer
 *               sleep                     msleep / usleep_range / udelay
 *   results   the last result, one line
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

static struct dentry *dir;
static DEFINE_MUTEX(run_lock);		/* one measurement at a time */
static char result[256];		/* protected by run_lock */

/* Statistics of one measurement, all in nanoseconds. */
struct stats {
	u64 min, max, sum;
	unsigned int n;
	unsigned int early;		/* samples that came too early */
	unsigned int hist[6];		/* <10us <50us <100us <500us <1ms >=1ms */
};

static void stats_add(struct stats *s, s64 v)
{
	static const s64 edge[] = { 10000, 50000, 100000, 500000, 1000000 };
	int i;

	if (v < 0) {
		s->early++;
		v = 0;
	}
	if (!s->n || v < s->min)
		s->min = v;
	if (v > s->max)
		s->max = v;
	s->sum += v;
	s->n++;
	for (i = 0; i < ARRAY_SIZE(edge) && v >= edge[i]; i++)
		;
	s->hist[i]++;
}

/* Average in ns; 0 when nothing was recorded (no division by zero). */
static u64 stats_avg(const struct stats *s)
{
	return s->n ? div_u64(s->sum, s->n) : 0;
}

/* --------------------------------------------------------------- clocks */
static int clocks_show(struct seq_file *m, void *v)
{
	struct timespec64 ts;
	u64 t0, t1;
	int i;

	seq_printf(m, "HZ %d\n", HZ);
	seq_printf(m, "jiffies %lu (%u ms since boot, 32-bit view %u)\n", jiffies,
		   jiffies_to_msecs(jiffies - INITIAL_JIFFIES), (u32)jiffies);
	/*
	 * TODO T1: Print one line each for CLOCK_MONOTONIC (ktime_get_ns),
	 *   CLOCK_BOOTTIME (ktime_get_boottime_ns), CLOCK_MONOTONIC_RAW
	 *   (ktime_get_raw_ns) and CLOCK_REALTIME (ktime_get_real_ts64,
	 *   printed as seconds.nanoseconds), then the hrtimer resolution
	 *   (hrtimer_resolution, in ns), and the average cost of one
	 *   ktime_get_ns() call over 1000 calls: "ktime_get_ns cost %llu ns".
	 *   Deck 04 slides 14-17.
	 */
	t0 = t1 = 0;
	i = 0;
	ts.tv_sec = 0;
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(clocks);

/* ------------------------------------------------------- timer_list run */
static struct {
	struct timer_list timer;
	unsigned long period;		/* jiffies */
	unsigned int want;
	ktime_t last;			/* time of the previous callback */
	struct stats interval;		/* time between callbacks, ns */
	long late_min, late_max;	/* jiffies - timer->expires at callback */
	unsigned int early;		/* callbacks with jiffies < expires */
	struct completion done;
} tl;

static void tl_fn(struct timer_list *t)
{
	ktime_t now = ktime_get();
	long late = (long)(jiffies - t->expires);

	/*
	 * TODO T2: Record one sample: lateness in jiffies (jiffies -
	 *   t->expires: late_min/late_max, and count "early" if negative)
	 *   and the interval since the previous callback in ns
	 *   (stats_add(&tl.interval, ...)).  Then complete(&tl.done) after
	 *   tl.want samples, or re-arm.  Re-arm relative to the previous
	 *   expiry (t->expires + period), not to "now": why?
	 */
	complete(&tl.done);
}

static int run_timer(unsigned int period_ms, unsigned int n)
{
	memset(&tl.interval, 0, sizeof(tl.interval));
	tl.early = 0;
	tl.want = n;
	tl.period = msecs_to_jiffies(period_ms);
	init_completion(&tl.done);
	timer_setup(&tl.timer, tl_fn, 0);
	tl.last = ktime_get();
	mod_timer(&tl.timer, jiffies + tl.period);
	if (wait_for_completion_interruptible(&tl.done)) {
		timer_delete_sync(&tl.timer);
		return -EINTR;
	}
	timer_delete_sync(&tl.timer);

	snprintf(result, sizeof(result),
		 "timer period_ms=%u jiffies=%lu n=%u late_jiffies=%ld..%ld early=%u interval_us min=%llu avg=%llu max=%llu\n",
		 period_ms, tl.period, tl.interval.n, tl.late_min, tl.late_max, tl.early,
		 div_u64(tl.interval.min, 1000),
		 div_u64(stats_avg(&tl.interval), 1000),
		 div_u64(tl.interval.max, 1000));
	return 0;
}

/* ---------------------------------------------------------- hrtimer run */
static struct {
	struct hrtimer timer;
	ktime_t period;
	unsigned int want;
	bool hardirq;			/* callback ran in hard-IRQ context */
	struct stats lat;		/* actual expiry - programmed expiry */
	struct completion done;
} hr;

static enum hrtimer_restart hr_fn(struct hrtimer *t)
{
	/*
	 * TODO T3: lateness = now - hrtimer_get_expires(t), in ns.  Record it,
	 *   note whether we run in hard-IRQ context (in_hardirq()), then
	 *   stop after hr.want samples (complete, HRTIMER_NORESTART) or
	 *   advance the expiry with hrtimer_forward_now() and restart.
	 *   Deck 04 slide 23.
	 */
	complete(&hr.done);
	return HRTIMER_NORESTART;
}

static int run_hrtimer(unsigned int period_us, unsigned int n)
{
	memset(&hr.lat, 0, sizeof(hr.lat));
	hr.want = n;
	hr.period = ns_to_ktime((u64)period_us * NSEC_PER_USEC);
	init_completion(&hr.done);
	/* v6.12 API; v6.13 adds hrtimer_setup(timer, fn, clock, mode). */
	hrtimer_init(&hr.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr.timer.function = hr_fn;
	hrtimer_start(&hr.timer, hr.period, HRTIMER_MODE_REL);
	if (wait_for_completion_interruptible(&hr.done)) {
		hrtimer_cancel(&hr.timer);
		return -EINTR;
	}
	hrtimer_cancel(&hr.timer);

	snprintf(result, sizeof(result),
		 "hrtimer period_us=%u n=%u hardirq=%d late_us min=%llu avg=%llu max=%llu early=%u hist=%u,%u,%u,%u,%u,%u\n",
		 period_us, hr.lat.n, hr.hardirq, div_u64(hr.lat.min, 1000),
		 div_u64(stats_avg(&hr.lat), 1000), div_u64(hr.lat.max, 1000),
		 hr.lat.early, hr.lat.hist[0], hr.lat.hist[1], hr.lat.hist[2],
		 hr.lat.hist[3], hr.lat.hist[4], hr.lat.hist[5]);
	return 0;
}

/* ---------------------------------------------------------------- delays */
static u64 time_us(void (*fn)(unsigned long), unsigned long arg)
{
	u64 t0 = ktime_get_ns();

	fn(arg);
	return div_u64(ktime_get_ns() - t0, 1000);
}

static void do_msleep(unsigned long ms) { msleep(ms); }
static void do_usleep(unsigned long us) { usleep_range(us, us + us / 10); }
static void do_udelay(unsigned long us) { udelay(us); }

static int run_sleep(void)
{
	/*
	 * TODO T4: Measure msleep(1), msleep(10), usleep_range(1000, 1100) and
	 *   udelay(100) with time_us() and report them in one line:
	 *   "sleep msleep1_us=%llu msleep10_us=%llu usleep1000_us=%llu
	 *   udelay100_us=%llu".  Explain msleep(1) from HZ (Deck 04 slide 20).
	 */
	snprintf(result, sizeof(result), "sleep not implemented\n");
	return 0;
}

/* --------------------------------------------------------------- debugfs */
static ssize_t run_write(struct file *f, const char __user *ubuf, size_t count,
			 loff_t *ppos)
{
	char buf[64], cmd[16];
	unsigned int a = 0, b = 0;
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	if (sscanf(buf, "%15s %u %u", cmd, &a, &b) < 1)
		return -EINVAL;

	if (mutex_lock_interruptible(&run_lock))
		return -EINTR;
	if (!strcmp(cmd, "timer") && a >= 1 && a <= 5000 && b >= 1 && b <= 1000)
		ret = run_timer(a, b);
	else if (!strcmp(cmd, "hrtimer") && a >= 50 && a <= 1000000 && b >= 1 && b <= 100000)
		ret = run_hrtimer(a, b);
	else if (!strcmp(cmd, "sleep"))
		ret = run_sleep();
	else
		ret = -EINVAL;
	mutex_unlock(&run_lock);
	return ret ? ret : count;
}

static const struct file_operations run_fops = {
	.owner = THIS_MODULE,
	.write = run_write,
};

static int results_show(struct seq_file *m, void *v)
{
	mutex_lock(&run_lock);
	seq_puts(m, result);
	mutex_unlock(&run_lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(results);

static int __init lfdtick_init(void)
{
	dir = debugfs_create_dir("lfdtick", NULL);
	debugfs_create_file("clocks", 0444, dir, NULL, &clocks_fops);
	debugfs_create_file("run", 0200, dir, NULL, &run_fops);
	debugfs_create_file("results", 0444, dir, NULL, &results_fops);
	return 0;
}

static void __exit lfdtick_exit(void)
{
	debugfs_remove_recursive(dir);
}

module_init(lfdtick_init);
module_exit(lfdtick_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clocks, timers and delays measured (LFD430 Lab 09)");
