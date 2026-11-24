#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 00 acceptance tests: is the lab environment complete?
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
cfg() { zcat /proc/config.gz | grep -q "^$1=y"; }
dmesg_reset
check_grep "kernel is v6.12 built for the labs" "^6\.12\.[0-9]+-lfd430" uname -r
check "/lab is the shared repository (9p)" grep -q " /lab 9p " /proc/mounts
check "debugfs is mounted" test -d /sys/kernel/debug/tracing -o -d /sys/kernel/tracing
check "the kernel configuration is available (/proc/config.gz)" test -e /proc/config.gz
if [ "$(uname -m)" = x86_64 ]; then
	for o in CONFIG_KASAN CONFIG_PROVE_LOCKING CONFIG_DEBUG_ATOMIC_SLEEP \
		 CONFIG_DEBUG_KMEMLEAK CONFIG_DMA_API_DEBUG CONFIG_DYNAMIC_DEBUG \
		 CONFIG_FUNCTION_GRAPH_TRACER CONFIG_PREEMPT_DYNAMIC CONFIG_GDB_SCRIPTS; do
		check "$o=y" cfg $o
	done
	check_grep "PREEMPT_DYNAMIC: the model is chosen at boot (preempt=)" "PREEMPT_DYNAMIC" uname -v
fi
check "lfd_hello loads" insmod ./lfd_hello.ko
check_grep "its message is in the kernel log" "lfd_hello: hello from Linux 6\.12" dmesg_since
check "lfd_hello unloads" unload lfd_hello
check_clean_log all
summary
