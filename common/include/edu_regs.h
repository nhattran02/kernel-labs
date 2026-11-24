/* SPDX-License-Identifier: GPL-2.0 */
/*
 * edu_regs.h - register map of QEMU's "edu" PCI teaching device.
 *
 * Source: QEMU docs/specs/edu.rst and hw/misc/edu.c (identical in QEMU 4.2
 * and 8.2 except where noted in the lab book).  Usable from kernel modules
 * and from user space (Lab 12).
 *
 * BAR0: 1 MiB of MMIO.  Offsets below 0x80 must be accessed with 32-bit
 * accesses; the DMA registers at 0x80 and above accept 32- or 64-bit.
 */
#ifndef EDU_REGS_H
#define EDU_REGS_H

#define EDU_VENDOR_ID		0x1234
#define EDU_DEVICE_ID		0x11e8

#define EDU_REG_ID		0x00	/* RO: 0xRRrr00ed, major.minor version */
#define EDU_REG_LIVENESS	0x04	/* RW: reads back the bitwise NOT */
#define EDU_REG_FACT		0x08	/* RW: write n, read n! (32-bit) */
#define EDU_REG_STATUS		0x20	/* RW */
#define  EDU_STATUS_COMPUTING	0x01	/* RO: factorial in progress */
#define  EDU_STATUS_IRQFACT	0x80	/* raise EDU_IRQ_FACT when done */
#define EDU_REG_IRQ_STATUS	0x24	/* RO: pending interrupt sources */
#define EDU_REG_IRQ_RAISE	0x60	/* WO: OR value into IRQ_STATUS, raise */
#define EDU_REG_IRQ_ACK		0x64	/* WO: clear bits; must be done by the ISR */
#define EDU_REG_DMA_SRC		0x80	/* RW 64-bit */
#define EDU_REG_DMA_DST		0x88	/* RW 64-bit */
#define EDU_REG_DMA_CNT		0x90	/* RW 64-bit */
#define EDU_REG_DMA_CMD		0x98	/* RW 64-bit */
#define  EDU_DMA_RUN		0x01	/* start; cleared by the device when done */
#define  EDU_DMA_FROM_DEV	0x02	/* 0: RAM -> edu buffer, 1: edu buffer -> RAM */
#define  EDU_DMA_IRQ		0x04	/* raise EDU_IRQ_DMA when done */

#define EDU_IRQ_FACT		0x00000001
#define EDU_IRQ_DMA		0x00000100

#define EDU_DMA_BUF		0x40000	/* device-side address of the buffer */
#define EDU_DMA_BUF_SIZE	4096
#define EDU_DMA_MASK_BITS	28	/* default dma_mask of the model */

#endif /* EDU_REGS_H */
