#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 09 acceptance tests.  Timing values vary from machine to machine (and
# a lot under TCG emulation): the checks test invariants, not numbers.
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
D=/sys/kernel/debug/lfdtick
rmmod lfdtick 2>/dev/null
dmesg_reset
check "lfdtick loads" insmod ./lfdtick.ko
cat $D/clocks
check_grep "HZ is 250 in the lab kernel" "^HZ 250$" cat $D/clocks
check_grep "hrtimer resolution is 1 ns (high-resolution mode)" "^hrtimer_resolution 1 ns$" cat $D/clocks
check_grep "ktime_get_ns cost reported" "^ktime_get_ns cost [0-9]+ ns$" cat $D/clocks

field() { sed -n "s/.* $1=\([0-9.]*\).*/\1/p" $D/results; }

echo "timer 10 25" > $D/run; cat $D/results
check_grep "10 ms is 3 jiffies at HZ=250 (rounded up)" "jiffies=3 " cat $D/results
check "a timer_list never fires early" test "$(field early)" = 0
check_grep "lateness is counted in whole jiffies, never negative" "late_jiffies=[0-9]+\.\.[0-9]+ " cat $D/results

echo "timer 1000 3" > $D/run; cat $D/results
check "a 1 s timer is not early either" test "$(field early)" = 0
check_grep "1 s is 250 jiffies" "jiffies=250 " cat $D/results

echo "hrtimer 1000 500" > $D/run; cat $D/results
check "an hrtimer never fires early" test "$(field early)" = 0
check_grep "the hrtimer callback runs in hard-IRQ context" "hardirq=1" cat $D/results
check_grep "500 samples taken" " n=500 " cat $D/results

echo sleep > $D/run; cat $D/results
check "msleep(1) sleeps at least 1 ms" test "$(field msleep1_us)" -ge 1000
check "msleep(10) sleeps at least 10 ms" test "$(field msleep10_us)" -ge 10000
check "usleep_range(1000, 1100) sleeps at least 1 ms" test "$(field usleep1000_us)" -ge 1000
check "udelay(100) waits at least ~100 us" test "$(field udelay100_us)" -ge 99
check_fail "bad command rejected" sh -c "echo 'hrtimer 1 1' > $D/run"
check "lfdtick unloads" unload lfdtick
check_clean_log all
summary
