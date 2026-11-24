#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 08 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfdpipe 2>/dev/null
dmesg_reset
check "lfdpipe loads" insmod ./lfdpipe.ko
run_prog_bg 120 "lfdpipe_test" ./test/lfdpipe_test
[ -z "$LFD_STUCK" ] && check "lfdpipe unloads" unload lfdpipe
check_clean_log all
summary
