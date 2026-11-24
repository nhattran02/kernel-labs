// SPDX-License-Identifier: GPL-2.0
/*
 * lfd8139 - a driver for the Realtek RTL8139 Fast Ethernet controller
 * (LFD430 Lab 21, capstone: PCI + DMA + interrupts + NAPI + netdev).
 *
 * Classic (non-C+) mode, as described in the RTL8139 datasheet:
 *  - RX: one contiguous ring of RX_BUF_LEN bytes (+ pad, WRAP mode).  The
 *    chip writes [status:16][length:16][frame][CRC:32] records, each
 *    aligned to 4 bytes; software advances CAPR (RxBufPtr) - minus 16,
 *    a well-known quirk of this chip.
 *  - TX: four bounce buffers; writing a buffer's size to TxStatusN hands
 *    it to the chip, which sets TxStatOK when done.
 * Tested against QEMU's model (hw/net/rtl8139.c).  Compare every step with
 * drivers/net/ethernet/realtek/8139too.c, which handles real-chip errata
 * that this lab driver leaves out.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#define DRV_NAME	"lfd8139"

/* Registers (offsets in the MMIO BAR) */
enum {
	MAC0 = 0x00, MAR0 = 0x08, TxStatus0 = 0x10, TxAddr0 = 0x20, RxBuf = 0x30,
	ChipCmd = 0x37, RxBufPtr = 0x38, RxBufAddr = 0x3a, IntrMask = 0x3c,
	IntrStatus = 0x3e, TxConfig = 0x40, RxConfig = 0x44, RxMissed = 0x4c,
	Cfg9346 = 0x50, Config1 = 0x52, MediaStatus = 0x58,
};
/* ChipCmd */
enum { CmdReset = 0x10, CmdRxEnb = 0x08, CmdTxEnb = 0x04, RxBufEmpty = 0x01 };
/* IntrStatus / IntrMask */
enum {
	PCIErr = 0x8000, RxFIFOOver = 0x40, LinkChg = 0x20, RxOverflow = 0x10,
	TxErr = 0x08, TxOK = 0x04, RxErr = 0x02, RxOK = 0x01,
	RxAckBits = RxFIFOOver | RxOverflow | RxOK,
	IrqNapiBits = RxAckBits | RxErr | TxOK | TxErr,
	IrqAll = IrqNapiBits | LinkChg | PCIErr,
};
/* TxStatusN */
enum { TxStatOK = 0x8000, TxUnderrun = 0x4000, TxAborted = 0x40000000 };
/* RX header status */
enum { RxStatusOK = 0x0001 };
/* RxConfig */
enum {
	AcceptBroadcast = 0x08, AcceptMulticast = 0x04, AcceptMyPhys = 0x02,
	RxWrap = 1 << 7,		/* 8139too calls it RxNoWrap: no wrap-around */
	RxCfgDMAShift = 8, RxCfgFIFOShift = 13,
};
enum { Cfg9346_Lock = 0x00, Cfg9346_Unlock = 0xc0 };
enum { MSRLinkFail = 0x04 };

#define RX_BUF_LEN	8192			/* RBLEN = 00 */
#define RX_BUF_TOT	(RX_BUF_LEN + 16 + 2048)	/* + pad for WRAP mode */
#define NUM_TX_DESC	4
#define TX_BUF_SIZE	1792			/* >= MTU + headers, as 8139too */
#define TX_FLAG		((256 << 11) & 0x003f0000)	/* early-TX threshold */
#define RX_CONFIG	(RxWrap | (7 << RxCfgFIFOShift) | (7 << RxCfgDMAShift) | \
			 AcceptBroadcast | AcceptMulticast | AcceptMyPhys)
#define TX_CONFIG	((3 << 24) | (6 << 8) | (8 << 4))	/* IFG96, DMA 1K, retry */

struct lfd8139 {
	struct pci_dev *pdev;
	struct net_device *dev;
	void __iomem *io;
	struct napi_struct napi;

	/*
	 * Two locks for two kinds of context.  tx_lock: TX ring indices, used
	 * by ndo_start_xmit (BHs off) and NAPI poll (softirq) only.  irq_lock:
	 * IntrMask, also written by the hard-IRQ handler, so other users must
	 * disable interrupts.  (One lock for both, taken without disabling
	 * IRQs in xmit, deadlocks: lockdep reports "inconsistent lock state".)
	 */
	spinlock_t tx_lock;
	spinlock_t irq_lock;
	void *rx_ring;
	dma_addr_t rx_dma;
	unsigned int cur_rx;		/* offset of the next RX record */
	void *tx_bufs;
	dma_addr_t tx_dma;
	unsigned int cur_tx, dirty_tx;	/* free-running; slot = n % NUM_TX_DESC */
};

/* ---------------------------------------------------------------- link */
static void lfd8139_check_link(struct lfd8139 *tp)
{
	if (ioread8(tp->io + MediaStatus) & MSRLinkFail)
		netif_carrier_off(tp->dev);
	else
		netif_carrier_on(tp->dev);
}

/* ---------------------------------------------------------- hardware init */
static int lfd8139_reset(struct lfd8139 *tp)
{
	int i;

	/*
	 * TODO T2: Soft reset: write CmdReset to ChipCmd, then poll until the
	 *   chip clears the bit (up to 1000 x 10 us; -ETIMEDOUT otherwise).
	 */
	(void)i;
	return -ENOSYS;
}

static void lfd8139_hw_start(struct lfd8139 *tp)
{
	int i;

	/*
	 * TODO T3: Program the chip (8139too: rtl8139_hw_start):
	 *    1. RxBuf = rx_dma (where the RX ring lives)
	 *    2. ChipCmd = CmdRxEnb | CmdTxEnb, then RxConfig = RX_CONFIG,
	 *       TxConfig = TX_CONFIG (config is written while enabled)
	 *    3. TxAddr0..3 = tx_dma + i * TX_BUF_SIZE
	 *    4. accept all multicast: MAR0..MAR0+7 = 0xff
	 *    5. RxMissed = 0; cur_rx = cur_tx = dirty_tx = 0
	 *    6. clear stale events (IntrStatus = 0xffff, write-1-to-clear)
	 *       and unmask IrqAll in IntrMask
	 */
	(void)i;
}

/* ---------------------------------------------------------------- TX */
static netdev_tx_t lfd8139_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lfd8139 *tp = netdev_priv(dev);
	unsigned int entry, len = skb->len;
	void *buf;

	/*
	 * TODO T4: One frame into the next TX bounce buffer.
	 *    - too long for TX_BUF_SIZE: count a tx_dropped, free, OK.
	 *    - entry = cur_tx % NUM_TX_DESC; copy the frame with
	 *      skb_copy_bits(); pad short frames with zeros to ETH_ZLEN
	 *      (the chip does not pad); free the skb.
	 *    - under tp->tx_lock: make the copy visible to the device before
	 *      the doorbell (wmb()), write TX_FLAG | len to TxStatus0 +
	 *      4 * entry, cur_tx++, and stop the queue when all four
	 *      buffers are in flight (cur_tx - dirty_tx == NUM_TX_DESC).
	 *   Deck 07 slides 15-16, Deck 06 slide 23.
	 */
	(void)tp; (void)entry; (void)len; (void)buf;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/* Reclaim finished TX buffers.  Called from NAPI poll. */
static void lfd8139_tx_complete(struct lfd8139 *tp)
{
	struct net_device *dev = tp->dev;
	u32 status;

	/*
	 * TODO T5: Walk from dirty_tx to cur_tx: read TxStatus of the slot;
	 *   stop at the first one the chip has not finished (none of
	 *   TxStatOK | TxUnderrun | TxAborted set).  Count tx_packets and
	 *   tx_bytes (status & 0x1fff) or tx_errors; dirty_tx++.  Wake the
	 *   queue if it was stopped and a slot is free.  Under tp->tx_lock.
	 */
	(void)dev; (void)status;
}

/* ---------------------------------------------------------------- RX */
static int lfd8139_rx(struct lfd8139 *tp, int budget)
{
	struct net_device *dev = tp->dev;
	int received = 0;

	/*
	 * TODO T6: The receive loop (8139too: rtl8139_rx), while received <
	 *   budget and ChipCmd does not say RxBufEmpty:
	 *    - dma_rmb(): read the record only after the empty check
	 *    - off = cur_rx % RX_BUF_LEN; status = le32 at rx_ring + off;
	 *      size = status >> 16 (frame + 4-byte CRC)
	 *    - bad record (!(status & RxStatusOK), size < 8 + 4, size >
	 *      TX_BUF_SIZE + 4): count rx_errors, resynchronise by
	 *      setting cur_rx = RxBufAddr (what the chip wrote so far),
	 *      and leave the loop
	 *    - napi_alloc_skb(size - 4), skb_put_data from off + 4,
	 *      eth_type_trans, napi_gro_receive, count RX bytes
	 *    - cur_rx = ALIGN(cur_rx + size + 4, 4); RxBufPtr =
	 *      (u16)(cur_rx - 16); ack RxAckBits in IntrStatus
	 */
	(void)dev; (void)budget;
	return received;
}

static int lfd8139_poll(struct napi_struct *napi, int budget)
{
	struct lfd8139 *tp = container_of(napi, struct lfd8139, napi);
	unsigned long flags;
	int work;

	lfd8139_tx_complete(tp);
	work = lfd8139_rx(tp, budget);
	/*
	 * TODO T7: Done before the budget ran out: napi_complete_done(), then
	 *   re-enable the interrupts the ISR masked (IntrMask = IrqAll,
	 *   under tp->irq_lock with IRQs off).  Why only after completing?
	 */
	(void)flags;
	return work;
}

static irqreturn_t lfd8139_isr(int irq, void *data)
{
	struct net_device *dev = data;
	struct lfd8139 *tp = netdev_priv(dev);
	u16 status;

	/*
	 * TODO T7: The hard IRQ handler of a NAPI driver does almost nothing:
	 *    - status = IntrStatus; 0 (shared line, not ours) or 0xffff
	 *      (device gone) -> IRQ_NONE
	 *    - ack the non-RX events now (write status & ~RxAckBits);
	 *      RX bits are acked by the RX loop
	 *    - LinkChg: re-check the link; PCIErr: log it
	 *    - RX/TX events: mask them (IntrMask = LinkChg | PCIErr) under
	 *      tp->irq_lock and napi_schedule()
	 *   Deck 07 slides 21-22.
	 */
	(void)tp; (void)status;
	return IRQ_NONE;
}

/* ---------------------------------------------------------- open / stop */
static int lfd8139_open(struct net_device *dev)
{
	struct lfd8139 *tp = netdev_priv(dev);
	struct device *d = &tp->pdev->dev;
	int ret;

	/*
	 * TODO T3: Allocate the RX ring (RX_BUF_TOT) and the four TX buffers
	 *   (NUM_TX_DESC * TX_BUF_SIZE) as coherent DMA memory, request the
	 *   (shared, INTx) IRQ, reset and start the chip, enable NAPI,
	 *   start the queue, check the link.  Unwind on errors.
	 */
	(void)d; (void)ret;
	return -ENOSYS;
}

static int lfd8139_stop(struct net_device *dev)
{
	struct lfd8139 *tp = netdev_priv(dev);
	struct device *d = &tp->pdev->dev;

	/*
	 * TODO T3: Reverse order: stop the queue, disable NAPI, silence the
	 *   chip (IntrMask = 0, ChipCmd = 0), free the IRQ, then the DMA
	 *   memory - only now is the chip guaranteed not to write into it.
	 */
	(void)tp; (void)d;
	return 0;
}

static const struct net_device_ops lfd8139_ops = {
	.ndo_open = lfd8139_open,
	.ndo_stop = lfd8139_stop,
	.ndo_start_xmit = lfd8139_xmit,
	.ndo_get_stats64 = dev_get_tstats64,
	.ndo_validate_addr = eth_validate_addr,
};

static void lfd8139_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct lfd8139 *tp = netdev_priv(dev);

	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(tp->pdev), sizeof(info->bus_info));
}

static const struct ethtool_ops lfd8139_ethtool_ops = {
	.get_drvinfo = lfd8139_get_drvinfo,
	.get_link = ethtool_op_get_link,
};

/* ----------------------------------------------------------- probe/remove */
static int lfd8139_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *d = &pdev->dev;
	struct net_device *dev;
	struct lfd8139 *tp;
	u8 mac[ETH_ALEN];
	int i, ret;

	/*
	 * TODO T1: PCI bring-up (Deck 06 slides 7-11): pcim_enable_device();
	 *   BAR1 is the MMIO window (BAR0 is the same registers in I/O
	 *   space) - check its flags and map it with pcim_iomap_region();
	 *   32-bit DMA (dma_set_mask_and_coherent); pci_set_master().
	 *   Then alloc_etherdev(sizeof(*tp)), SET_NETDEV_DEV(), fill tp,
	 *   read the MAC address from MAC0..MAC0+5 (ioread8) and set it with
	 *   eth_hw_addr_set() (random if invalid), NAPI, ops, per-CPU stats,
	 *   register_netdev().  The PCI part is devres-managed; the netdev
	 *   is not.
	 */
	(void)d; (void)dev; (void)tp; (void)mac; (void)i; (void)ret;
	return -ENOSYS;
}

static void lfd8139_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	unregister_netdev(dev);		/* calls ndo_stop if the interface is up */
	free_netdev(dev);
}

static const struct pci_device_id lfd8139_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8139) },
	{ }
};
MODULE_DEVICE_TABLE(pci, lfd8139_ids);

static struct pci_driver lfd8139_driver = {
	.name = DRV_NAME,
	.id_table = lfd8139_ids,
	.probe = lfd8139_probe,
	.remove = lfd8139_remove,
};
module_pci_driver(lfd8139_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTL8139 Fast Ethernet driver (LFD430 Lab 21)");
