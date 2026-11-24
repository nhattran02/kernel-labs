// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_gpio_loop - GPIO interrupt latency on the BeagleBone Black (Lab 16).
 *
 * A jumper wire connects an output GPIO to an input GPIO (see
 * dts/lfd-gpio-loop.dtso).  The driver drives the output high and measures
 * how long it takes until the input's rising-edge interrupt runs.
 *
 * Module parameter threaded=1 handles the edge in a threaded handler.
 * debugfs /sys/kernel/debug/lfd_gpio_loop/:
 *   run      write N (1..100000): N measurements, blocks until done
 *   results  "n=.. missed=.. spurious=.. lat_us min=.. avg=.. max=.. hist=..."
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/mutex.h>

static bool threaded;
module_param(threaded, bool, 0444);
MODULE_PARM_DESC(threaded, "Handle the edge in a threaded IRQ handler");

struct loop {
	struct device *dev;
	struct gpio_desc *out, *in;
	int irq;
	struct dentry *dir;
	struct mutex run_lock;		/* one measurement run at a time */

	spinlock_t lock;		/* protects the fields below */
	ktime_t t_set;			/* output driven high at; 0 = idle */
	ktime_t t_irq;			/* hard-IRQ entry time */
	struct completion edge;
	unsigned int n, missed, spurious;
	u64 lat_min, lat_max, lat_sum;
	unsigned int hist[6];		/* <10 <20 <50 <100 <500 >=500 us */
};

static void record(struct loop *l, ktime_t now)
{
	static const u64 edge_ns[] = { 10000, 20000, 50000, 100000, 500000 };
	u64 lat;
	int i;

	if (!l->t_set) {
		l->spurious++;
		return;
	}
	lat = ktime_to_ns(ktime_sub(now, l->t_set));
	l->t_set = 0;
	if (!l->n || lat < l->lat_min)
		l->lat_min = lat;
	if (lat > l->lat_max)
		l->lat_max = lat;
	l->lat_sum += lat;
	l->n++;
	for (i = 0; i < ARRAY_SIZE(edge_ns) && lat >= edge_ns[i]; i++)
		;
	l->hist[i]++;
	complete(&l->edge);
}

static irqreturn_t loop_hardirq(int irq, void *data)
{
	/*
	 * TODO T3: Hard-IRQ handler.  Take the timestamp first thing.  In
	 *   threaded mode store it in t_irq and return IRQ_WAKE_THREAD;
	 *   otherwise record() it under l->lock and return IRQ_HANDLED.
	 *   Deck 04 slides 8-10, 32.
	 */
	return IRQ_NONE;
}

static irqreturn_t loop_thread(int irq, void *data)
{
	/*
	 * TODO T4: Threaded part: measure at thread entry (ktime_get()), not at
	 *   t_irq, so that the result includes the wake-up of the IRQ thread.
	 *   This runs in process context with IRQs enabled: lock accordingly.
	 */
	return IRQ_NONE;
}

static int do_run(struct loop *l, unsigned int count)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&l->lock, flags);
	l->n = l->missed = l->spurious = 0;
	l->lat_min = l->lat_max = l->lat_sum = 0;
	memset(l->hist, 0, sizeof(l->hist));
	spin_unlock_irqrestore(&l->lock, flags);

	for (i = 0; i < count; i++) {
		gpiod_set_value(l->out, 0);
		usleep_range(500, 600);		/* let the line settle low */
		reinit_completion(&l->edge);
		/*
		 * TODO T3: Under l->lock with IRQs off: set t_set = ktime_get(),
		 *   then drive the output high.  Then wait up to 100 ms for
		 *   the edge; on timeout count a miss (and clear t_set).
		 */
		if (signal_pending(current))
			return -EINTR;
	}
	gpiod_set_value(l->out, 0);
	return 0;
}

static ssize_t run_write(struct file *f, const char __user *ubuf, size_t count,
			 loff_t *ppos)
{
	struct loop *l = file_inode(f)->i_private;
	unsigned int n;
	int ret;

	ret = kstrtouint_from_user(ubuf, count, 0, &n);
	if (ret || !n || n > 100000)
		return -EINVAL;
	mutex_lock(&l->run_lock);
	ret = do_run(l, n);
	mutex_unlock(&l->run_lock);
	return ret ? ret : count;
}

static const struct file_operations run_fops = {
	.owner = THIS_MODULE,
	.write = run_write,
};

static int results_show(struct seq_file *m, void *v)
{
	struct loop *l = m->private;
	unsigned long flags;

	spin_lock_irqsave(&l->lock, flags);
	seq_printf(m, "mode=%s n=%u missed=%u spurious=%u lat_us min=%llu avg=%llu max=%llu hist=%u,%u,%u,%u,%u,%u\n",
		   threaded ? "threaded" : "hardirq", l->n, l->missed, l->spurious,
		   div_u64(l->lat_min, 1000),
		   l->n ? div_u64(div_u64(l->lat_sum, l->n), 1000) : 0,
		   div_u64(l->lat_max, 1000), l->hist[0], l->hist[1], l->hist[2],
		   l->hist[3], l->hist[4], l->hist[5]);
	spin_unlock_irqrestore(&l->lock, flags);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(results);

/* Board self-test: is the wire there?  Catches most bring-up mistakes. */
static int wire_test(struct loop *l)
{
	int lo, hi;

	gpiod_set_value(l->out, 1);
	udelay(10);
	hi = gpiod_get_value(l->in);
	gpiod_set_value(l->out, 0);
	udelay(10);
	lo = gpiod_get_value(l->in);
	return (hi == 1 && lo == 0) ? 0 : -ENODEV;
}

static int loop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct loop *l;
	int ret;

	l = devm_kzalloc(dev, sizeof(*l), GFP_KERNEL);
	if (!l)
		return -ENOMEM;
	l->dev = dev;
	spin_lock_init(&l->lock);
	mutex_init(&l->run_lock);
	init_completion(&l->edge);

	/*
	 * TODO T2: Get the two GPIOs by function name ("out" low, "in" as
	 *   input) with devm_gpiod_get(); a missing GPIO controller gives
	 *   -EPROBE_DEFER: let it through with dev_err_probe().  This lab
	 *   sets the output with interrupts disabled: refuse GPIOs that can
	 *   sleep (gpiod_cansleep()).  Run wire_test() and fail with a clear
	 *   message if the jumper is missing.  Get the input's IRQ with
	 *   gpiod_to_irq() and request it for the RISING edge: hard handler
	 *   only, or loop_hardirq + loop_thread with IRQF_ONESHOT when
	 *   threaded.  Deck 05 slides 24-27.
	 */
	(void)ret; (void)wire_test;
	return -ENOSYS;

	platform_set_drvdata(pdev, l);
	l->dir = debugfs_create_dir("lfd_gpio_loop", NULL);
	debugfs_create_file("run", 0200, l->dir, l, &run_fops);
	debugfs_create_file("results", 0444, l->dir, l, &results_fops);
	dev_info(dev, "loopback OK, IRQ %d (%s)\n", l->irq, threaded ? "threaded" : "hard");
	return 0;
}

static void loop_remove(struct platform_device *pdev)
{
	struct loop *l = platform_get_drvdata(pdev);

	debugfs_remove_recursive(l->dir);
}

static const struct of_device_id loop_of_match[] = {
	{ .compatible = "lfd430,gpio-loopback" },
	{ }
};
MODULE_DEVICE_TABLE(of, loop_of_match);

static struct platform_driver loop_driver = {
	.probe = loop_probe,
	.remove = loop_remove,
	.driver = {
		.name = "lfd-gpio-loop",
		.of_match_table = loop_of_match,
	},
};
module_platform_driver(loop_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO interrupt latency, BeagleBone Black (LFD430 Lab 16)");
