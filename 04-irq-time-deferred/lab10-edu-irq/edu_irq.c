// SPDX-License-Identifier: GPL-2.0
/*
 * Interrupt handling for the edu device (LFD430 Lab 10).
 *
 * Deck 04 Part 1 (slides 6-14) and Deck 06 slide 12 are the background.
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include "edu.h"

static irqreturn_t edu_isr(int irq, void *data)
{
	/*
	 * TODO T2: The hard-IRQ handler.
	 *   1. Read EDU_REG_IRQ_STATUS.  0 means "not ours" (the INTx line
	 *      may be shared): count it in nnone and return IRQ_NONE.
	 *   2. Acknowledge exactly the bits you saw (EDU_REG_IRQ_ACK) - even
	 *      with MSI the device needs it.  What happens on a level-
	 *      triggered INTx line if you forget?
	 *   3. Under e->lock: count, store last_status, and if raise_ts is set
	 *      add (now - raise_ts) to the latency statistics, clear raise_ts.
	 *   4. If EDU_IRQ_FACT is set, complete(&e->fact_done).
	 */
	return IRQ_NONE;
}

int edu_irq_init(struct edu *e)
{
	/*
	 * TODO T1: Install edu_isr on e->irq with devm_request_irq(), name
	 *   "lfd-edu", dev_id e.  An INTx line may be shared with other
	 *   devices: which flag is then mandatory?  An MSI vector is never
	 *   shared.  Deck 04 slides 8-9.
	 */
	return 0;
}

void edu_raise(struct edu *e, u32 value)
{
	/*
	 * TODO T3: Record the time in e->raise_ts, then write value to
	 *   EDU_REG_IRQ_RAISE.  The ISR reads raise_ts under e->lock and may
	 *   run on this CPU the moment the write lands: which spin_lock
	 *   variant must process context use here?  Deck 03 slide 11.
	 */
}

int edu_factorial(struct edu *e, u32 n, u32 *result)
{
	/*
	 * TODO T4: Compute n! with the device, interrupt-driven:
	 *   serialise callers (e->fact_lock), reinit_completion(), set
	 *   EDU_STATUS_IRQFACT in EDU_REG_STATUS, write n to EDU_REG_FACT,
	 *   then wait_for_completion_timeout() (1 s -> -ETIMEDOUT) and read
	 *   the result from EDU_REG_FACT.  Why must the completion be re-
	 *   initialised *before* the write that starts the computation?
	 */
	return -ENOSYS;
}
