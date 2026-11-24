// SPDX-License-Identifier: GPL-2.0
/*
 * lfdnet - a pair of virtual Ethernet interfaces, lfd0 <-> lfd1 (Lab 20).
 *
 * Each interface models a NIC with an RX ring of RING_SIZE slots.  A frame
 * transmitted on one interface is placed in the peer's RX ring and the
 * peer's "RX interrupt" fires, which - as in a real NAPI driver - only
 * schedules NAPI; the poll function then delivers up to `budget` frames.
 * When the peer's ring is full the sender stops its queue; the peer's poll
 * wakes it again (TX flow control).  The carrier is up only while both
 * ends are up.
 *
 * Background: Deck 07 (all parts).  Compare with drivers/net/veth.c.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netdev_queues.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#define RING_SIZE	64
#define DRV_NAME	"lfdnet"

struct lfdnet_priv {
	struct net_device *dev;
	struct net_device *peer;
	struct napi_struct napi;
	struct sk_buff_head rxq;	/* the RX ring, filled by the peer */
	atomic64_t ring_full;		/* frames that found the ring full */
	atomic64_t napi_polls;
	atomic64_t budget_exhausted;	/* polls that used the whole budget */
};

static struct net_device *lfd[2];

/* ------------------------------------------------------------ data path */
static netdev_tx_t lfdnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lfdnet_priv *p = netdev_priv(dev);
	struct lfdnet_priv *pp = netdev_priv(p->peer);
	unsigned int len = skb->len;

	/*
	 * TODO T2: Transmit = put the frame in the peer's RX ring.
	 *    - no carrier or ring full: count a drop
	 *      (dev_core_stats_tx_dropped_inc, and ring_full in the peer),
	 *      free the skb, return NETDEV_TX_OK (never TX_BUSY here)
	 *    - skb_orphan(): the sending socket must not stay charged
	 *    - skb_queue_tail() into pp->rxq, count TX bytes
	 *      (dev_sw_netstats_tx_add)
	 *    - ring now full? Stop the queue WITHOUT losing a wake-up: the
	 *      peer's poll may drain the ring on another CPU between your
	 *      test and the stop.  Use netif_txq_maybe_stop() (stop, barrier,
	 *      re-check, restart) on netdev_get_tx_queue(dev, 0), with
	 *      free slots = RING_SIZE - skb_queue_len_lockless(), stop below
	 *      1 free slot, restart at RING_SIZE / 2.  Deck 07 slide 17.
	 *    - raise the peer's "RX interrupt": napi_schedule(&pp->napi)
	 *   Deck 07 slides 15-16, 21-22.
	 */
	(void)pp; (void)len;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int lfdnet_poll(struct napi_struct *napi, int budget)
{
	struct lfdnet_priv *p = container_of(napi, struct lfdnet_priv, napi);
	struct sk_buff *skb;
	int done = 0;

	atomic64_inc(&p->napi_polls);
	/*
	 * TODO T3: NAPI poll.  Deliver at most `budget` frames from p->rxq:
	 *   skb->protocol = eth_type_trans(skb, p->dev) (it also sets
	 *   skb->dev and pulls the Ethernet header), count RX bytes (the
	 *   frame length before the pull), napi_gro_receive().  Then wake
	 *   the peer's queue if it was stopped and our ring is at most half
	 *   full - after an smp_mb() that pairs with the barrier inside
	 *   netif_txq_maybe_stop() (why? Deck 07 slide 17).
	 *   If done < budget: napi_complete_done(); else count
	 *   budget_exhausted and stay scheduled.  Return done.
	 */
	(void)skb;
	napi_complete_done(napi, 0);
	return done;
}

/* --------------------------------------------------------- open / stop */
static int lfdnet_open(struct net_device *dev)
{
	struct lfdnet_priv *p = netdev_priv(dev);

	/*
	 * TODO T1: Enable NAPI and the TX queue.  The link is up only when
	 *   both ends are running: if the peer is up (netif_running), turn
	 *   the carrier on for both.  Deck 07 slides 7, 24.
	 */
	return 0;
}

static int lfdnet_stop(struct net_device *dev)
{
	struct lfdnet_priv *p = netdev_priv(dev);

	/*
	 * TODO T1: The reverse: both carriers off, stop the queue, disable
	 *   NAPI (waits for a running poll), then drop what is left in the
	 *   ring (skb_queue_purge).
	 */
	return 0;
}

static const struct net_device_ops lfdnet_ops = {
	.ndo_open = lfdnet_open,
	.ndo_stop = lfdnet_stop,
	.ndo_start_xmit = lfdnet_xmit,
	.ndo_get_stats64 = dev_get_tstats64,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
};

/* ---------------------------------------------------------------- ethtool */
static const char lfdnet_stat_names[][ETH_GSTRING_LEN] = {
	"rx_ring_full", "napi_polls", "napi_budget_exhausted",
};

static void lfdnet_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, "virtual", sizeof(info->bus_info));
}

static int lfdnet_get_sset_count(struct net_device *dev, int sset)
{
	return sset == ETH_SS_STATS ? ARRAY_SIZE(lfdnet_stat_names) : -EOPNOTSUPP;
}

static void lfdnet_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	int i;

	/* TODO T4: For ETH_SS_STATS, emit the names with ethtool_puts(). */
	(void)i; (void)data;
}

static void lfdnet_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data)
{
	struct lfdnet_priv *p = netdev_priv(dev);

	/* TODO T4: Fill data[] in the same order as the names. */
	(void)p; (void)data;
}

static const struct ethtool_ops lfdnet_ethtool_ops = {
	.get_drvinfo = lfdnet_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = lfdnet_get_sset_count,
	.get_strings = lfdnet_get_strings,
	.get_ethtool_stats = lfdnet_get_ethtool_stats,
};

/* ------------------------------------------------------------------ setup */
static struct net_device *lfdnet_create(void)
{
	struct net_device *dev;
	struct lfdnet_priv *p;

	/*
	 * TODO T1: alloc_etherdev() with our private area; name pattern "lfd%d"
	 *   (dev_alloc_name happens at registration if the name has %d);
	 *   netdev_ops, ethtool_ops, a random MAC (eth_hw_addr_random), per-
	 *   CPU software stats (pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS),
	 *   the RX ring (skb_queue_head_init) and NAPI (netif_napi_add).
	 *   Carrier off until opened.  Deck 07 slide 6.
	 */
	(void)dev; (void)p;
	return NULL;
}

static int __init lfdnet_init(void)
{
	int i, ret;

	for (i = 0; i < 2; i++) {
		lfd[i] = lfdnet_create();
		if (!lfd[i]) {
			ret = -ENOMEM;
			goto err_free;
		}
	}
	((struct lfdnet_priv *)netdev_priv(lfd[0]))->peer = lfd[1];
	((struct lfdnet_priv *)netdev_priv(lfd[1]))->peer = lfd[0];

	ret = register_netdev(lfd[0]);
	if (ret)
		goto err_free;
	ret = register_netdev(lfd[1]);
	if (ret) {
		unregister_netdev(lfd[0]);
		goto err_free;
	}
	pr_info("%s <-> %s\n", lfd[0]->name, lfd[1]->name);
	return 0;

err_free:
	for (i = 0; i < 2; i++)
		if (lfd[i])
			free_netdev(lfd[i]);
	return ret;
}

static void __exit lfdnet_exit(void)
{
	/* Both in one RTNL section: neither may run while the other is gone. */
	rtnl_lock();
	unregister_netdevice(lfd[0]);
	unregister_netdevice(lfd[1]);
	rtnl_unlock();
	free_netdev(lfd[0]);
	free_netdev(lfd[1]);
}

module_init(lfdnet_init);
module_exit(lfdnet_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual Ethernet pair with NAPI (LFD430 Lab 20)");
