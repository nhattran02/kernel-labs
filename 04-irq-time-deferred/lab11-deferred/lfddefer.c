// SPDX-License-Identifier: GPL-2.0
/*
 * lfddefer - bottom-half shoot-out (LFD430 Lab 11).
 *
 * A periodic hrtimer plays the role of a device interrupt: its callback
 * runs in hard-IRQ context, takes a timestamp ("the interrupt"), pushes
 * it into a FIFO and defers the processing with one of five mechanisms:
 *
 *   tasklet   tasklet_schedule()                  softirq (legacy API)
 *   bh        queue_work(system_bh_wq)            softirq (v6.9+)
 *   wq        queue_work(system_wq)               kworker, process context
 *   highpri   queue_work(system_highpri_wq)       kworker, nice -20
 *   kthread   wake up our own thread              process context, SCHED_FIFO
 *
 * The deferred function drains the FIFO and measures, for every event,
 * the latency from "interrupt" to processing.
 *
 * debugfs /sys/kernel/debug/lfddefer/:
 *   run      write "<mode> <events> [rate_hz]"; blocks until done
 *   results  "mode=M events=N calls=C coalesced=K ctx=softirq|task lat_us min avg max"
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/kfifo.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

enum mode { M_TASKLET, M_BH, M_WQ, M_HIGHPRI, M_KTHREAD, M_NR };
static const char *const mode_name[M_NR] = {
	"tasklet", "bh", "wq", "highpri", "kthread",
};

static struct {
	enum mode mode;
	unsigned int want;		/* events to generate */
	unsigned int produced;		/* hrtimer side */
	unsigned int consumed;		/* deferred side */
	unsigned int calls;		/* invocations of the deferred function */
	unsigned int coalesced;		/* schedule requests that found it pending */
	bool softirq_ctx, task_ctx;
	u64 lat_min, lat_max, lat_sum;
	ktime_t period;
	struct completion done;
} run;

/* DEFINE_KFIFO declares *and* initialises; DECLARE_KFIFO needs INIT_KFIFO. */
static DEFINE_KFIFO(events, u64, 1024);	/* single producer, single consumer */
static struct hrtimer source;
static struct tasklet_struct tasklet;
static struct work_struct work;		/* for system_wq / system_highpri_wq */
static struct work_struct bh_work;		/* for system_bh_wq: never share a */
						/* work item between BH and normal wqs */
static struct task_struct *thread;
static bool thread_kick;			/* set by producer, cleared by thread */
static DECLARE_WAIT_QUEUE_HEAD(thread_wq);

static DEFINE_MUTEX(run_lock);
static char result[200];
static struct dentry *dir;

/* Common processing, called by every mechanism. */
static void process_events(void)
{
	u64 t, now, lat;

	run.calls++;
	if (in_softirq())
		run.softirq_ctx = true;
	if (in_task() && !in_softirq())
		run.task_ctx = true;
	while (kfifo_get(&events, &t)) {
		now = ktime_get_ns();
		lat = now - t;
		if (!run.consumed || lat < run.lat_min)
			run.lat_min = lat;
		if (lat > run.lat_max)
			run.lat_max = lat;
		run.lat_sum += lat;
		if (++run.consumed == run.want)
			complete(&run.done);
	}
}

static void tasklet_fn(struct tasklet_struct *t)
{
	process_events();
}

static void work_fn(struct work_struct *w)
{
	process_events();
}

static int thread_fn(void *unused)
{
	/*
	 * TODO T4: Loop until kthread_should_stop(): sleep on thread_wq until
	 *   thread_kick is set or we must stop, clear thread_kick, then
	 *   process_events().  Why does the wait condition also test
	 *   kthread_should_stop()?  Deck 04 slide 31.
	 */
	return 0;
}

/* The "interrupt handler": hard-IRQ context. */
static enum hrtimer_restart source_fn(struct hrtimer *t)
{
	bool queued = true;

	if (!kfifo_put(&events, ktime_get_ns()))
		pr_warn_ratelimited("FIFO full, event lost\n");
	run.produced++;

	/*
	 * TODO T2: Defer the processing with the selected mechanism.  Count in
	 *   run.coalesced the requests that found the bottom half already
	 *   pending (queue_work() returns false; for the tasklet check
	 *   TASKLET_STATE_SCHED in t->state before scheduling; for the
	 *   kthread, thread_kick already true).  Deck 04 slides 26-32.
	 */
	if (!queued)
		run.coalesced++;

	if (run.produced == run.want)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, run.period);
	return HRTIMER_RESTART;
}

static int do_run(enum mode m, unsigned int n, unsigned int rate)
{
	unsigned long left;

	memset(&run, 0, sizeof(run));
	run.mode = m;
	run.want = n;
	run.period = ns_to_ktime(NSEC_PER_SEC / rate);
	init_completion(&run.done);
	kfifo_reset(&events);

	hrtimer_start(&source, run.period, HRTIMER_MODE_REL);
	left = wait_for_completion_timeout(&run.done, msecs_to_jiffies(30000));

	/*
	 * TODO T3: Shut down in the right order: first stop the source (the
	 *   hrtimer), then make sure no bottom half is pending or running
	 *   (cancel_work_sync / tasklet_kill as appropriate).  What could
	 *   happen with the opposite order?
	 */
	if (!left)
		return -ETIMEDOUT;

	snprintf(result, sizeof(result),
		 "mode=%s events=%u calls=%u coalesced=%u ctx=%s lat_us min=%llu avg=%llu max=%llu\n",
		 mode_name[m], run.consumed, run.calls, run.coalesced,
		 run.softirq_ctx ? (run.task_ctx ? "mixed" : "softirq") : "task",
		 div_u64(run.lat_min, 1000), div_u64(div_u64(run.lat_sum, run.consumed), 1000),
		 div_u64(run.lat_max, 1000));
	return 0;
}

static ssize_t run_write(struct file *f, const char __user *ubuf, size_t count,
			 loff_t *ppos)
{
	char buf[48], name[16];
	unsigned int n, rate = 1000;
	int m, ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	if (sscanf(buf, "%15s %u %u", name, &n, &rate) < 2 || !n || n > 100000 ||
	    rate < 10 || rate > 20000)
		return -EINVAL;
	m = match_string(mode_name, M_NR, name);
	if (m < 0)
		return -EINVAL;

	mutex_lock(&run_lock);
	ret = do_run(m, n, rate);
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

static int __init lfddefer_init(void)
{
	hrtimer_init(&source, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	source.function = source_fn;
	/*
	 * TODO T1: Initialise the tasklet (tasklet_setup), the two work
	 *   items (INIT_WORK: one for the BH workqueue, one for the others)
	 *   and start the kthread "lfddefer" (kthread_run);
	 *   make the thread SCHED_FIFO with sched_set_fifo().  Deck 04
	 *   slides 33-36.
	 */
	thread = NULL;

	dir = debugfs_create_dir("lfddefer", NULL);
	debugfs_create_file("run", 0200, dir, NULL, &run_fops);
	debugfs_create_file("results", 0444, dir, NULL, &results_fops);
	return 0;
}

static void __exit lfddefer_exit(void)
{
	debugfs_remove_recursive(dir);	/* no new runs after this */
	/* TODO T1: Stop the kthread (kthread_stop wakes it and waits). */
}

module_init(lfddefer_init);
module_exit(lfddefer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bottom-half mechanisms compared (LFD430 Lab 11)");
