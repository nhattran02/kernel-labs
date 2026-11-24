/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* lfd_edu.h - user-space ABI of /dev/eduN (LFD430 Labs 18-19, 23). */
#ifndef _UAPI_LFD_EDU_H
#define _UAPI_LFD_EDU_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define LFD_EDU_IRQ_INTX	0
#define LFD_EDU_IRQ_MSI		1

struct lfd_edu_info {
	__u32 version;		/* ID register: 0xRRrr00ed */
	__u32 irq_mode;		/* LFD_EDU_IRQ_* */
	__u64 bar_size;		/* BAR0 length in bytes */
	__u64 dma_mask;		/* streaming DMA mask in use (Lab 19) */
	__u32 dma_buf_size;	/* bytes reachable with read()/write() (Lab 19) */
	__u32 pad;
};

#define LFD_EDU_GET_INFO	_IOR(0xF5, 0x40, struct lfd_edu_info)
#define LFD_EDU_FACT		_IOWR(0xF5, 0x41, __u32)	/* in: n, out: n! */

#endif /* _UAPI_LFD_EDU_H */
