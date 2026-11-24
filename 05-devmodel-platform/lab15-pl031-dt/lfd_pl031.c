// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_pl031 - platform driver for the ARM PL031 RTC (LFD430 Lab 15).
 *
 * Bound through Device Tree (compatible "lfd430,pl031-lite", see
 * dts/lfd-pl031.dtso) and registered with the RTC subsystem, which gives
 * us /dev/rtcN, /sys/class/rtc/rtcN and hwclock support for free.
 *
 * Register map: ARM PrimeCell RTC (PL031) TRM; cross-check with QEMU's
 * hw/rtc/pl031.c and Linux's drivers/rtc/rtc-pl031.c.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/io.h>

#define RTC_DR		0x00	/* data: current time in seconds (RO) */
#define RTC_MR		0x04	/* match: alarm time */
#define RTC_LR		0x08	/* load: set the time */
#define RTC_CR		0x0c	/* control: bit 0 = start */
#define RTC_IMSC	0x10	/* interrupt mask set/clear: bit 0 = alarm */
#define RTC_RIS		0x14	/* raw interrupt status */
#define RTC_MIS		0x18	/* masked interrupt status */
#define RTC_ICR		0x1c	/* interrupt clear (WO) */
#define RTC_PERIPHID0	0xfe0	/* four bytes at 0xfe0, 0xfe4, 0xfe8, 0xfec */

#define RTC_BIT_AI	BIT(0)	/* alarm interrupt */

/*
 * Lab knob: a wakeup-capable RTC is pinned by the alarmtimer code
 * (kernel/time/alarmtimer.c), which then holds a reference on this module
 * for good.  ignore_wakeup=1 lets you exercise the unload path.
 */
static bool ignore_wakeup;
module_param(ignore_wakeup, bool, 0444);
MODULE_PARM_DESC(ignore_wakeup, "Ignore the DT wakeup-source property");

struct lfd_pl031 {
	void __iomem *base;
	struct rtc_device *rtc;
	int irq;
};

static u32 periph_id(void __iomem *base)
{
	u32 id = 0;
	int i;

	for (i = 0; i < 4; i++)
		id |= (readl(base + RTC_PERIPHID0 + 4 * i) & 0xff) << (8 * i);
	return id;
}

/* ------------------------------------------------------- rtc_class_ops */
static int lpl_read_time(struct device *dev, struct rtc_time *tm)
{
	struct lfd_pl031 *p = dev_get_drvdata(dev);

	/*
	 * TODO T3: Convert RTC_DR (seconds since the epoch) to *tm.
	 *   rtc_time64_to_tm().  Deck 05 slide 17 for readl().
	 */
	(void)p;
	return -ENOSYS;
}

static int lpl_set_time(struct device *dev, struct rtc_time *tm)
{
	struct lfd_pl031 *p = dev_get_drvdata(dev);

	/*
	 * TODO T3: Load the new time into RTC_LR (rtc_tm_to_time64()).  The
	 *   RTC core already rejected times outside [range_min, range_max].
	 */
	(void)p;
	return -ENOSYS;
}

static int lpl_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct lfd_pl031 *p = dev_get_drvdata(dev);

	/*
	 * TODO T4: Clear any pending alarm (RTC_ICR), then set or clear the
	 *   alarm bit in RTC_IMSC.
	 */
	(void)p;
	return -ENOSYS;
}

static int lpl_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct lfd_pl031 *p = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(p->base + RTC_MR), &alm->time);
	alm->pending = readl(p->base + RTC_RIS) & RTC_BIT_AI;
	alm->enabled = readl(p->base + RTC_IMSC) & RTC_BIT_AI;
	return 0;
}

static int lpl_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct lfd_pl031 *p = dev_get_drvdata(dev);

	/*
	 * TODO T4: Program RTC_MR with the alarm time, then enable or disable
	 *   the alarm interrupt as alm->enabled says.
	 */
	(void)p;
	return -ENOSYS;
}

static const struct rtc_class_ops lpl_ops = {
	.read_time = lpl_read_time,
	.set_time = lpl_set_time,
	.read_alarm = lpl_read_alarm,
	.set_alarm = lpl_set_alarm,
	.alarm_irq_enable = lpl_alarm_irq_enable,
};

static irqreturn_t lpl_irq(int irq, void *data)
{
	/*
	 * TODO T4: If RTC_MIS shows the alarm, clear it (RTC_ICR) and report
	 *   it with rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF); otherwise
	 *   IRQ_NONE.
	 */
	return IRQ_NONE;
}

/* ------------------------------------------------------------- probe */
static int lpl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lfd_pl031 *p;
	u32 id;
	int ret;

	/*
	 * TODO T2: The platform-driver checklist (Deck 05 slides 24-27):
	 *    1. devm_kzalloc the state, platform_set_drvdata()
	 *    2. map the registers: devm_platform_ioremap_resource(pdev, 0)
	 *    3. check the PrimeCell ID: (periph_id() & 0x000fffff) must be
	 *       0x00041031, else dev_err_probe(..., -ENODEV, ...)
	 *    4. start the counter: RTC_CR = 1; mask the alarm: RTC_IMSC = 0
	 *    5. p->irq = platform_get_irq(pdev, 0)
	 *    6. devm_rtc_allocate_device(); set ops, range_max = U32_MAX
	 *       (a 32-bit seconds counter)
	 *    7. devm_request_irq(..., lpl_irq, 0, "lfd-pl031", p)
	 *    8. unless ignore_wakeup: if the DT property "wakeup-source"
	 *       is present (device_property_read_bool()):
	 *       device_init_wakeup(dev, true)
	 *    9. devm_rtc_register_device()  - last: it makes us visible
	 */
	(void)p; (void)id; (void)ret; (void)periph_id;
	return -ENOSYS;
}

/*
 * TODO T2: The match table (Deck 05 slide 25): one entry,
 *   "lfd430,pl031-lite", and MODULE_DEVICE_TABLE(of, ...).
 */
static const struct of_device_id lpl_of_match[] = {
	{ }
};

static struct platform_driver lpl_driver = {
	.probe = lpl_probe,
	.driver = {
		.name = "lfd-pl031",
		.of_match_table = lpl_of_match,
	},
};
module_platform_driver(lpl_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PL031 RTC platform driver (LFD430 Lab 15)");
