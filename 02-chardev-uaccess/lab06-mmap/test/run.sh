#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 06 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfdshm 2>/dev/null
dmesg_reset
check_fail "mode=bogus is rejected" insmod ./lfdshm.ko mode=bogus
check "lfdshm loads (mode=remap)" insmod ./lfdshm.ko
run_prog "remap mode" ./test/lfdshm_test remap
./test/lfdshm_test hold 4 &
sleep 1
check_fail "rmmod is refused while a process maps the buffer" unload lfdshm
wait
check "rmmod after the mapping is gone" unload lfdshm
check "lfdshm loads (mode=fault)" insmod ./lfdshm.ko mode=fault pages=32
run_prog "fault mode" ./test/lfdshm_test fault
check "rmmod" unload lfdshm
check_clean_log all
summary
