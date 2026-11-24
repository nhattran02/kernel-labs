#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 07 acceptance tests: a stress run with lockdep, KASAN and DEBUG_LIST
# watching.  Every kernel report counts as a failure.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfdstat 2>/dev/null
quiet_console
dmesg_reset
check "lfdstat loads" insmod ./lfdstat.ko
run_prog_bg 120 "stress: 4 writers x 20000 events" ./test/lfdstat_stress 4 20000
[ -z "$LFD_STUCK" ] && check "lfdstat unloads" unload lfdstat
check_clean_log all
if grep -q "debug_locks: *1" /proc/lockdep_stats; then
	check "lockdep is still enabled (nothing tainted the kernel)" true
else
	check "lockdep is still enabled (nothing tainted the kernel)" false
fi
summary
