#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Lab 15 acceptance tests (QEMU arm64 virt, booted with virt-lfd.dtb).
. /lab/common/lib.sh
cd "$(dirname "$0")/.." || exit 1
N=/proc/device-tree/pl031@9010000
rmmod lfd_pl031 2>/dev/null
dmesg_reset
check_grep "the DT node carries our compatible" "lfd430,pl031-lite" cat $N/compatible
check "the overlay added wakeup-source" test -e $N/wakeup-source
check_grep "it is a platform device (no arm,primecell)" "9010000.pl031" ls /sys/bus/platform/devices
check_grep "the platform device's modalias comes from DT" "^of:N.*Clfd430,pl031-lite" cat /sys/bus/platform/devices/9010000.pl031/modalias
# --- 1st load: ignore wakeup-source, exercise the full life cycle ---
check "lfd_pl031 loads (ignore_wakeup=1)" insmod ./lfd_pl031.ko ignore_wakeup=1
check "bound to the lfd-pl031 driver" test -e /sys/bus/platform/drivers/lfd-pl031/9010000.pl031
check_grep "probe message with the PrimeCell revision" "PL031 rev 1, IRQ" dmesg_since
check "/dev/rtc0 exists" test -c /dev/rtc0
check_grep "rtc0 is ours" "lfd-pl031" cat /sys/class/rtc/rtc0/name
check_grep "busybox hwclock reads it" "[0-9]{2}:[0-9]{2}:[0-9]{2}" hwclock -r
run_prog "rtc_test" ./test/rtc_test
check_grep "the alarm interrupt was counted" "lfd-pl031" cat /proc/interrupts
grep lfd-pl031 /proc/interrupts
check "lfd_pl031 unloads" unload lfd_pl031
check "/dev/rtc0 removed" test ! -e /dev/rtc0
check "the driver is gone from the platform bus" test ! -e /sys/bus/platform/drivers/lfd-pl031

# --- 2nd load: honour wakeup-source ---
check "lfd_pl031 loads (wakeup-source honoured)" insmod ./lfd_pl031.ko
check "power/wakeup exists (needs CONFIG_PM_SLEEP)" test -e /sys/bus/platform/devices/9010000.pl031/power/wakeup
check_grep "alarmtimer picked our RTC" "alarmtimer" ls /sys/bus/platform/devices
check_grep "... and holds a module reference" "^1$" cat /sys/module/lfd_pl031/refcnt
check_fail "so the module can no longer be unloaded" unload lfd_pl031
check_clean_log all
summary
