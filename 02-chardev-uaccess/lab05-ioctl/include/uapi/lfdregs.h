/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * lfdregs.h - user-space ABI of /dev/lfdregs (LFD430 Lab 05).
 *
 * The device exposes LFDREGS_NREGS 32-bit registers.  Register 0 is a
 * read-only identification register reading LFDREGS_ID.
 *
 * Rules for this header (Deck 02 slides 34-37):
 *  - only fixed-width __u8/__u16/__u32/__u64 types, no pointers, no long;
 *  - the same layout for 32-bit and 64-bit user space (no hidden padding);
 *  - user pointers travel as __u64.
 */
#ifndef _UAPI_LFDREGS_H
#define _UAPI_LFDREGS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define LFDREGS_ABI_VERSION	1
#define LFDREGS_NREGS		64
#define LFDREGS_ID		0x4c464430	/* "LFD0" */

/* 0xF5 is unused in Documentation/userspace-api/ioctl/ioctl-number.rst. */
#define LFDREGS_IOC_MAGIC	0xF5

struct lfdregs_info {
	__u32 abi_version;	/* LFDREGS_ABI_VERSION */
	__u32 nregs;		/* LFDREGS_NREGS */
};

struct lfdregs_reg {
	__u32 index;
	__u32 value;
};

/* Copy registers [first, first + count) to the user buffer at buf. */
/*
 * TODO T2: This layout is wrong for a UAPI header.  Find both problems,
 *   fix them, and explain why the 32-bit test failed before the fix.
 *   Compare sizeof(struct lfdregs_bulk) with gcc -m32 and -m64.
 */
struct lfdregs_bulk {
	__u32 first;
	void *buf;
	__u32 count;
};

#define LFDREGS_GET_INFO	_IOR(LFDREGS_IOC_MAGIC, 0x00, struct lfdregs_info)
#define LFDREGS_READ		_IOWR(LFDREGS_IOC_MAGIC, 0x01, struct lfdregs_reg)
#define LFDREGS_WRITE		_IOW(LFDREGS_IOC_MAGIC, 0x02, struct lfdregs_reg)
#define LFDREGS_BULK_READ	_IOW(LFDREGS_IOC_MAGIC, 0x03, struct lfdregs_bulk)
#define LFDREGS_RESET		_IO(LFDREGS_IOC_MAGIC, 0x04)	/* CAP_SYS_ADMIN */

#endif /* _UAPI_LFDREGS_H */
