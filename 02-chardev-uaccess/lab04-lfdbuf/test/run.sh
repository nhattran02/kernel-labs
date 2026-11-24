#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 04 acceptance tests.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
rmmod lfdbuf 2>/dev/null
dmesg_reset
check_fail "ndevs=0 is rejected" insmod ./lfdbuf.ko ndevs=0
check_fail "ndevs=9 is rejected" insmod ./lfdbuf.ko ndevs=9
check_fail "size=0 is rejected"  insmod ./lfdbuf.ko size=0
check "lfdbuf loads with 3 devices of 8192 bytes" insmod ./lfdbuf.ko ndevs=3 size=8192
check_grep "major number registered as 'lfdbuf'" "^ *[0-9]+ lfdbuf$" cat /proc/devices
check "device nodes created by devtmpfs" test -c /dev/lfdbuf0 -a -c /dev/lfdbuf1 -a -c /dev/lfdbuf2
check_grep "class directory exists" "lfdbuf2" ls /sys/class/lfdbuf
check_grep "dev attribute gives major:minor 0..2" ":2$" cat /sys/class/lfdbuf/lfdbuf2/dev
run_prog "lfdbuf_test" ./test/lfdbuf_test
check "rmmod lfdbuf" unload lfdbuf
check "device nodes removed" test ! -e /dev/lfdbuf0
check_fail "major released" grep -q " lfdbuf$" /proc/devices
check_clean_log all
summary
