/* SPDX-License-Identifier: GPL-2.0 */
/* Shared state of the Lab 10 driver (edu_glue.c + edu_irq.c). */
#ifndef LFD_EDU_H
#define LFD_EDU_H

#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/ktime.h>
#include "edu_regs.h"

struct edu {
	struct pci_dev *pdev;
	void __iomem *bar;		/* BAR0, 1 MiB MMIO */
	int irq;			/* Linux IRQ number (INTx or MSI) */
	bool msi;

	spinlock_t lock;		/* protects everything up to fact_lock */
	u64 nirq;			/* interrupts that were ours */
	u64 nnone;			/* calls that were not ours (shared INTx) */
	u32 last_status;		/* last non-zero IRQ_STATUS seen */
	ktime_t raise_ts;		/* when edu_raise() wrote IRQ_RAISE */
	u64 lat_min, lat_max, lat_sum, lat_n;	/* raise -> handler, ns */

	struct mutex fact_lock;		/* one factorial at a time */
	struct completion fact_done;	/* completed by the ISR on EDU_IRQ_FACT */
};

/* Implemented in edu_irq.c (your work). */
int edu_irq_init(struct edu *e);
void edu_raise(struct edu *e, u32 value);
int edu_factorial(struct edu *e, u32 n, u32 *result);

#endif /* LFD_EDU_H */
