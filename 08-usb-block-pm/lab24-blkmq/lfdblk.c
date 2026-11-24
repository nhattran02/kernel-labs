// SPDX-License-Identifier: GPL-2.0
/*
 * lfdblk - a multi-queue RAM disk (LFD430 Lab 24).
 *
 * /dev/lfdblk0 is backed by vmalloc'ed memory.  Requests arrive through
 * blk-mq (Deck 08 Part 3).  Two completion models:
 *   delay_us=0   complete inline in ->queue_rq() (like a synchronous RAM disk)
 *   delay_us=N   complete N us later from an hrtimer, the way a real device
 *                completes from its interrupt handler
 * Fault injection: requests touching sector fail_sector fail with an I/O
 * error.  Discard (blkdiscard) zeroes the range.
 *
 * Compare with drivers/block/brd.c (bio-based RAM disk) and
 * drivers/block/null_blk/ (the blk-mq test driver).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hrtimer.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

static unsigned int capacity_mb = 64;
module_param(capacity_mb, uint, 0444);
MODULE_PARM_DESC(capacity_mb, "Disk size in MiB (default 64)");

static unsigned int nr_queues = 2;
module_param(nr_queues, uint, 0444);
MODULE_PARM_DESC(nr_queues, "Hardware queues (default 2)");

static unsigned int delay_us;
module_param(delay_us, uint, 0444);
MODULE_PARM_DESC(delay_us, "Completion delay in us (0: complete inline)");

static long fail_sector = -1;
module_param(fail_sector, long, 0644);
MODULE_PARM_DESC(fail_sector, "Fail requests that touch this sector (-1: off)");

struct lfdblk {
	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;
	u8 *data;
	sector_t sectors;
	int major;
};

/* Per-request driver data (blk-mq allocates it after struct request). */
struct lfdblk_cmd {
	struct hrtimer timer;
	blk_status_t status;
};

static struct lfdblk *lb;

/* Copy one request's data.  Returns BLK_STS_OK or an error status. */
static blk_status_t lfdblk_do_rq(struct lfdblk *b, struct request *rq)
{
	sector_t pos = blk_rq_pos(rq);
	unsigned int len = blk_rq_bytes(rq);
	struct req_iterator iter;
	struct bio_vec bv;
	void *p;

	/*
	 * TODO T2: The data path.
	 *    - bounds: pos + sectors must fit in b->sectors (BLK_STS_IOERR)
	 *    - fault injection: fail_sector inside [pos, pos + len/512)
	 *    - REQ_OP_DISCARD: memset the range to 0
	 *    - REQ_OP_READ / REQ_OP_WRITE: rq_for_each_segment(bv, rq,
	 *      iter): map the page with bvec_kmap_local(&bv), memcpy
	 *      to/from b->data + (pos << SECTOR_SHIFT), kunmap_local(),
	 *      advance pos by bv.bv_len >> SECTOR_SHIFT
	 *    - REQ_OP_FLUSH: nothing to do (RAM); others: BLK_STS_NOTSUPP
	 *   Deck 08 slide 27.
	 */
	(void)pos; (void)len; (void)iter; (void)bv; (void)p;
	return BLK_STS_NOTSUPP;
}

static enum hrtimer_restart lfdblk_timer(struct hrtimer *t)
{
	struct lfdblk_cmd *cmd = container_of(t, struct lfdblk_cmd, timer);

	/*
	 * TODO T4: "Interrupt" of the virtual device: hand the request back
	 *   to blk-mq with blk_mq_complete_request(); blk_mq_rq_from_pdu()
	 *   turns the pdu back into the request.
	 */
	(void)cmd;
	return HRTIMER_NORESTART;
}

/*
 * blk-mq calls this after blk_mq_complete_request(), possibly on the CPU
 * that submitted the request.
 */
static void lfdblk_complete(struct request *rq)
{
	struct lfdblk_cmd *cmd = blk_mq_rq_to_pdu(rq);

	blk_mq_end_request(rq, cmd->status);
}

static blk_status_t lfdblk_queue_rq(struct blk_mq_hw_ctx *hctx,
				    const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct lfdblk_cmd *cmd = blk_mq_rq_to_pdu(rq);

	/*
	 * TODO T3: Start the request (blk_mq_start_request), do the work.
	 *   With delay_us == 0 end it right here (blk_mq_end_request).
	 *   Otherwise keep the status in the pdu and arm cmd->timer
	 *   (relative, delay_us); the timer completes it.  Return BLK_STS_OK
	 *   - the request was accepted, whatever its final status.
	 *   This may run concurrently on several CPUs, even for one hctx.
	 */
	(void)cmd;
	return BLK_STS_NOTSUPP;
}

/* Called once per request when the tag set is allocated. */
static int lfdblk_init_request(struct blk_mq_tag_set *set, struct request *rq,
			       unsigned int hctx_idx, unsigned int numa_node)
{
	struct lfdblk_cmd *cmd = blk_mq_rq_to_pdu(rq);

	hrtimer_init(&cmd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cmd->timer.function = lfdblk_timer;
	return 0;
}

static const struct blk_mq_ops lfdblk_mq_ops = {
	.queue_rq = lfdblk_queue_rq,
	.complete = lfdblk_complete,
	.init_request = lfdblk_init_request,
};

static const struct block_device_operations lfdblk_fops = {
	.owner = THIS_MODULE,
};

static int __init lfdblk_init(void)
{
	struct queue_limits lim = {
		.logical_block_size = 512,
		.max_hw_discard_sectors = UINT_MAX >> SECTOR_SHIFT,
	};
	int ret;

	if (!capacity_mb || capacity_mb > 1024 || !nr_queues || nr_queues > 16)
		return -EINVAL;
	lb = kzalloc(sizeof(*lb), GFP_KERNEL);
	if (!lb)
		return -ENOMEM;
	lb->sectors = (sector_t)capacity_mb << (20 - SECTOR_SHIFT);
	lb->data = vzalloc((size_t)capacity_mb << 20);
	if (!lb->data) {
		ret = -ENOMEM;
		goto err_free;
	}

	lb->major = register_blkdev(0, "lfdblk");
	if (lb->major < 0) {
		ret = lb->major;
		goto err_data;
	}

	/*
	 * TODO T1: The tag set (Deck 08 slide 25): ops, nr_hw_queues,
	 *   queue_depth 64, numa_node NUMA_NO_NODE, cmd_size = sizeof(
	 *   struct lfdblk_cmd), flags BLK_MQ_F_SHOULD_MERGE;
	 *   blk_mq_alloc_tag_set().  Then the disk (slide 29):
	 *   blk_mq_alloc_disk(&tag_set, &lim, lb) (v6.12 signature: limits
	 *   are passed in), major/first_minor/minors = 1, fops, name
	 *   "lfdblk0", set_capacity(), add_disk() - it can fail.
	 */
	(void)lim;
	ret = -ENOSYS;
	goto err_blkdev;
	pr_info("lfdblk0: %u MiB, %u hw queues, %s completion\n", capacity_mb,
		nr_queues, delay_us ? "timer" : "inline");
	return 0;

err_blkdev:
	unregister_blkdev(lb->major, "lfdblk");
err_data:
	vfree(lb->data);
err_free:
	kfree(lb);
	return ret;
}

static void __exit lfdblk_exit(void)
{
	/*
	 * TODO T1: del_gendisk() (waits for I/O, removes /dev/lfdblk0),
	 *   put_disk(), free the tag set; then the rest.  Deck 08 slide 26.
	 */
	unregister_blkdev(lb->major, "lfdblk");
	vfree(lb->data);
	kfree(lb);
}

module_init(lfdblk_init);
module_exit(lfdblk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multi-queue RAM disk (LFD430 Lab 24)");
