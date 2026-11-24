/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* lfdstat.h - user-space ABI of /dev/lfdstat (LFD430 Lab 07). */
#ifndef _UAPI_LFDSTAT_H
#define _UAPI_LFDSTAT_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define LFDSTAT_IOC_MAGIC	0xF5

struct lfdstat_stats {
	__u64 count;		/* events accounted */
	__u64 sum;		/* sum of their values */
	__u32 min, max;		/* smallest / largest value (0 if none) */
	__u32 nrecords;		/* records currently kept */
	__u32 pad;
};

struct lfdstat_cfg_arg {
	__u32 max_records;	/* 1..4096 */
	char tag[16];		/* NUL-terminated */
	__u32 pad;
};

struct lfdstat_snapshot {
	struct lfdstat_stats stats;
	char tag[16];
	__u64 generation;	/* bumped by every SET_CFG */
};

#define LFDSTAT_GET_STATS	_IOR(LFDSTAT_IOC_MAGIC, 0x30, struct lfdstat_stats)
#define LFDSTAT_SET_CFG		_IOW(LFDSTAT_IOC_MAGIC, 0x31, struct lfdstat_cfg_arg)
#define LFDSTAT_SNAPSHOT	_IOR(LFDSTAT_IOC_MAGIC, 0x32, struct lfdstat_snapshot)
#define LFDSTAT_RESET		_IO(LFDSTAT_IOC_MAGIC, 0x33)

#endif /* _UAPI_LFDSTAT_H */
