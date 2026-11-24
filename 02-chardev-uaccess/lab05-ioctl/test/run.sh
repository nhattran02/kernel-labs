#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 05 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfdregs 2>/dev/null
dmesg_reset
check "lfdregs loads" insmod ./lfdregs.ko
check_grep "misc device node is world read/write" "^crw-rw-rw-" ls -l /dev/lfdregs
check_grep "registered as a misc device" "lfdregs" cat /proc/misc
run_prog "64-bit user space" ./test/lfdregs_test
run_prog "32-bit user space (compat)" ./test/lfdregs_test32
check "lfdregs unloads" unload lfdregs
check_clean_log all
summary
