/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* lfdshm.h - user-space ABI of /dev/lfdshm (LFD430 Lab 06). */
#ifndef _UAPI_LFDSHM_H
#define _UAPI_LFDSHM_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define LFDSHM_IOC_MAGIC	0xF5	/* shared with Lab 05, distinct numbers */

struct lfdshm_info {
	__u64 size;		/* bytes that can be mapped */
	__u64 faults;		/* page faults served (mode=fault only) */
};

/*
 * Fill a page-aligned user buffer with a byte pattern from the kernel,
 * through pinned pages, and report the page frame numbers that were pinned.
 */
struct lfdshm_fill {
	__u64 addr;		/* page-aligned user address */
	__u64 len;		/* multiple of the page size, at most 64 pages */
	__u64 pfns;		/* user pointer to len / page size __u64 */
	__u32 pattern;		/* low byte is used */
	__u32 pad;		/* must be zero */
};

#define LFDSHM_GET_INFO	_IOR(LFDSHM_IOC_MAGIC, 0x20, struct lfdshm_info)
#define LFDSHM_FILL_USER	_IOW(LFDSHM_IOC_MAGIC, 0x21, struct lfdshm_fill)

#endif /* _UAPI_LFDSHM_H */
