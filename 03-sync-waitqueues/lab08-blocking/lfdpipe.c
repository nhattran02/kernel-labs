// SPDX-License-Identifier: GPL-2.0
/*
 * lfdpipe - a bounded FIFO with blocking I/O and poll (LFD430 Lab 08).
 *
 * /dev/lfdpipe behaves like a pipe:
 *  - read() blocks while the FIFO is empty; it returns 0 (end of file)
 *    when the FIFO is empty and no writer has the device open;
 *  - write() blocks while the FIFO is full and may be short;
 *  - with O_NONBLOCK both return -EAGAIN instead of blocking;
 *  - a signal interrupts a blocked read()/write() (-ERESTARTSYS);
 *  - poll()/select()/epoll report EPOLLIN, EPOLLOUT and EPOLLHUP.
 *
 * Module parameter: size (FIFO capacity, rounded up to a power of two).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>

static unsigned int size = 4096;
module_param(size, uint, 0444);
MODULE_PARM_DESC(size, "FIFO capacity in bytes (power of two, default 4096)");

struct lfdpipe {
	struct mutex lock;		/* protects fifo and nwriters */
	struct kfifo fifo;
	unsigned int used;		/* kfifo_len(), mirrored for lockless checks */
	unsigned int nwriters;		/* opens with FMODE_WRITE */
	wait_queue_head_t readq;	/* readers wait for data or EOF */
	wait_queue_head_t writeq;	/* writers wait for space */
};

static struct lfdpipe pipe;

/*
 * The wait_event*() conditions run without pipe.lock (they cannot take a
 * mutex: the task is no longer TASK_RUNNING).  They read snapshots that
 * are updated with WRITE_ONCE() under the lock; the result is re-checked
 * under the lock.
 */
static bool can_read(struct lfdpipe *p)
{
	return READ_ONCE(p->used) || !READ_ONCE(p->nwriters);
}

static bool can_write(struct lfdpipe *p)
{
	return READ_ONCE(p->used) < kfifo_size(&p->fifo);
}

static void update_used(struct lfdpipe *p)
{
	WRITE_ONCE(p->used, kfifo_len(&p->fifo));
}

static int lfdpipe_open(struct inode *inode, struct file *f)
{
	/*
	 * TODO T1: Count writers: a file opened with FMODE_WRITE is a writer.
	 *   The FIFO is not seekable: use stream_open() (Deck 02 slide 14).
	 */
	return stream_open(inode, f);
}

static int lfdpipe_release(struct inode *inode, struct file *f)
{
	/*
	 * TODO T1: When the last writer goes away, readers blocked on an empty
	 *   FIFO must wake up and see end of file.
	 */
	return 0;
}

static ssize_t lfdpipe_read(struct file *f, char __user *ubuf, size_t count,
			    loff_t *ppos)
{
	unsigned int copied = 0;
	int ret;

	if (!count)
		return 0;
	/*
	 * TODO T2: Wait until there is data or no writer is left; with
	 *   O_NONBLOCK return -EAGAIN instead of waiting.  Never sleep
	 *   holding pipe.lock; re-check the condition after re-locking
	 *   (another reader may have been faster).  If the wait is
	 *   interrupted by a signal return -ERESTARTSYS.  Then copy with
	 *   kfifo_to_user(), update_used(), unlock and wake the writers.
	 *   Deck 03 slides 27-29.
	 */
	ret = -ENOSYS;
	return copied ? copied : ret;
}

static ssize_t lfdpipe_write(struct file *f, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	unsigned int copied = 0;
	int ret;

	if (!count)
		return 0;
	/*
	 * TODO T3: The mirror image of read(): wait for space, copy as much as
	 *   fits (a short write is fine), wake the readers.
	 */
	ret = -ENOSYS;
	return copied ? copied : ret;
}

static __poll_t lfdpipe_poll(struct file *f, struct poll_table_struct *wait)
{
	__poll_t mask = 0;

	/*
	 * TODO T4: Register both wait queues with poll_wait(), then report
	 *   EPOLLIN|EPOLLRDNORM when data is available, EPOLLOUT|EPOLLWRNORM
	 *   when there is space, EPOLLHUP when no writer is left.  poll()
	 *   must not block.  Deck 03 slide 31.
	 */
	return mask;
}

static const struct file_operations lfdpipe_fops = {
	.owner = THIS_MODULE,
	.open = lfdpipe_open,
	.release = lfdpipe_release,
	.read = lfdpipe_read,
	.write = lfdpipe_write,
	.poll = lfdpipe_poll,
};

static struct miscdevice lfdpipe_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lfdpipe",
	.fops = &lfdpipe_fops,
	.mode = 0666,
};

static int __init lfdpipe_init(void)
{
	int ret;

	if (size < 16 || size > (1U << 20))
		return -EINVAL;
	mutex_init(&pipe.lock);
	init_waitqueue_head(&pipe.readq);
	init_waitqueue_head(&pipe.writeq);
	ret = kfifo_alloc(&pipe.fifo, size, GFP_KERNEL);
	if (ret)
		return ret;
	ret = misc_register(&lfdpipe_misc);
	if (ret) {
		kfifo_free(&pipe.fifo);
		return ret;
	}
	pr_info("capacity %u bytes\n", kfifo_size(&pipe.fifo));
	return 0;
}

static void __exit lfdpipe_exit(void)
{
	misc_deregister(&lfdpipe_misc);
	kfifo_free(&pipe.fifo);
}

module_init(lfdpipe_init);
module_exit(lfdpipe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blocking FIFO with poll support (LFD430 Lab 08)");
